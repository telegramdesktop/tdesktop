/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "twidget/widget_host.h"

#include <crl/crl_time.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The widget host of an armed widget message: the options its Job got,
// with an optional measurement override, and what the host reported through
// onChange (stats, nodes, load, death, diags, error logs), merged on its
// host thread into a record that holds no host; and the renderer work of
// its RendererCore, timed by scopes a disposable overlay places there.
// Reference: docs/helpers/test_widget_host.md.

namespace Test {

// The options Tgw::Runtime::construct() puts into the Job that builds the
// widget host for the message |id|: its HistoryItem::globalId(). Returns
// |options| itself, moved through, unless Active() and |id| is armed by
// ArmWidgetHostWatch(); an unarmed id returns after one lookup of the armed
// map, with no clock read, lock or insertion. For an armed id it counts the
// call, replaces only the measurement options the watch's override gives,
// keeps every other field as the product set it, and records the
// measurement fields of the result, the options exactly as they enter the
// Job. Main thread. Without the harness this module defines this function
// and WatchWidgetHost() alone, as the identity and a no-op.
[[nodiscard]] twidget::WidgetHostOptions OverrideWidgetHostOptions(
	GlobalMsgId id,
	twidget::WidgetHostOptions options);

// Tgw::Runtime::constructed() calls this for each host it accepts, after its
// stale-build checks and right before RendererCore::bindHost() and
// WidgetHost::start(). Does nothing unless Active() and |id| is armed; an
// unarmed id returns after one lookup, with no clock read, lock,
// diagnostics copy or callback change. For an armed id it adds a record for
// this host to the watch and, in one synchronous WidgetHost::onHostThread()
// call, merges the host's diagnostics() into it and sets the host's
// onChange to merge every later change into that record alone, so no
// change falls between the two. The callback captures the record, the
// host's creation time by value and the product's earlier onChange if one
// was set; never the host or anything that holds it or its slot. |core| is
// the RendererCore bound to the host: kept as an address, never
// dereferenced, and registered for WidgetWorkScope until the watch is
// disarmed or another attachment registers the same address. Main thread.
void WatchWidgetHost(
	GlobalMsgId id,
	twidget::WidgetHost &host,
	const void *core);

// Everything below is harness-only: declared here and deliberately left
// undefined in a build without the harness, because no production code
// reads it.

// One armed message's watch: its override, the options of each armed
// construction, one record per attached host and the work log of their
// cores. Opaque; read it with ReadWidgetHost() and ReadWidgetWork().
class WidgetHostWatch;

// A watch's work log, shared with the scopes recording into it. Opaque.
class WidgetWorkLog;

// The measurement options a watch replaces; an absent field keeps the
// product's value. Visibility, locale, device pixel ratio, content safe
// area, storage, data and context always stay the product's.
struct WidgetHostOverride {
	std::optional<std::string> policy;
	std::optional<std::vector<std::string>> features;
	std::optional<size_t> memoryLimitBytes;
	std::optional<double> cpuLimitMs;
	std::optional<double> cpuQuotaMs;
	std::optional<double> cpuQuotaWindowMs;
	std::optional<double> mainThreadQuotaMs;
	std::optional<int> maxTimers;
};

// The measurement fields of the options one armed construction put into
// its Job, read after the override. Never storage, data, context or ids.
struct WidgetHostApplied {
	std::string policy;
	std::vector<std::string> features;
	std::string locale;
	twidget::WidgetContentSafeArea contentSafeArea;
	size_t memoryLimitBytes = 0;
	double cpuLimitMs = 0.;
	double cpuQuotaMs = 0.;
	double cpuQuotaWindowMs = 0.;
	double cpuKillAfterMs = 0.;
	double mainThreadQuotaMs = 0.;
	double devicePixelRatio = 0.;
	int maxTimers = 0;
	bool visible = false;
};

// One attached host's record: its diagnostics() at the attachment, then
// each onChange merged in order. Every `...At` is a crl::profile() stamp,
// 0 until its event: the attachment, the merge that last changed `stats`,
// the merge that first saw `loaded` and the one that first saw `dead`.
struct WidgetHostSample {
	// The host's last stats, and when a merge last found them changed.
	twidget::SandboxStats stats;
	crl::profile_time statsChangedAt = 0;
	// Kind -> count, as the host's diags have them.
	std::map<std::string, int> diags;
	// The first 20 console texts of level "error", verbatim, in order.
	std::vector<std::string> errors;
	// The first dead reason seen, verbatim.
	std::optional<std::string> dead;
	// The host's own loadedMs: from its creation to `loaded`.
	std::optional<double> loadedMs;
	// Steady-clock ms from the host's creation to the merge that first saw
	// `dead`: the clock and origin of loadedMs.
	std::optional<double> diedAtMs;
	crl::profile_time attachedAt = 0;
	crl::profile_time loadedAt = 0;
	crl::profile_time deadAt = 0;
	// The |core| address WatchWidgetHost() got.
	quintptr core = 0;
	// The host's last node count.
	int nodes = 0;
	// onChange calls merged, the attachment's snapshot not counted, and
	// those that changed `stats`.
	int changes = 0;
	int statsChanges = 0;
	// Console entries the host kept (it keeps at most 5000), and those of
	// level "error" among them, uncapped.
	int logs = 0;
	int errorLogs = 0;
	bool loaded = false;
};

// One watch, never merged across hosts: `applied` has one entry per armed
// OverrideWidgetHostOptions() call and `hosts` one per WatchWidgetHost()
// attachment, in order. Only one of each describes one host built with
// those options; more name a replaced or rebuilt host.
struct WidgetHostReading {
	std::vector<WidgetHostApplied> applied;
	std::vector<WidgetHostSample> hosts;
};

// Main thread. Arms |id|, the session-qualified id the message's
// HistoryItem::globalId() will return, for every construction from now on,
// with |measure| applied to its options, and returns its watch; arming an
// armed id returns the watch it already has, its override unchanged.
[[nodiscard]] std::shared_ptr<WidgetHostWatch> ArmWidgetHostWatch(
	GlobalMsgId id,
	WidgetHostOverride measure = {});

// Main thread. Later constructions for |id| keep the product's options and
// get no watch; hosts already attached keep updating their own records.
// Every core the watch registered is unregistered: their scopes record
// nothing more. Pass the id that was armed, as kept by the caller.
void DisarmWidgetHostWatch(GlobalMsgId id);

// Main thread. A snapshot; each host's record is copied under its own lock.
[[nodiscard]] WidgetHostReading ReadWidgetHost(const WidgetHostWatch &watch);

// What one WidgetWorkScope timed, by where the overlay puts it.
enum class WidgetWorkKind : uint8 {
	// RendererCore::hostMessage() past its stopped, requestFrame and frozen
	// returns: one host message applied and its consequences.
	Message,
	// RendererCore::tick(), whole.
	Tick,
	// RendererCore::frame() past its cached-frame return: one attempt at a
	// picture, which a size the list has not laid out yet ends before the
	// QPainter replay (WidgetWorkEvent::replayed tells).
	Frame,
};

// One outermost scope of a registered core. Both stamps are crl::profile()
// (monotonic microseconds, on the main thread): `started` the scope's last
// act of construction, `finished` the first act of its destruction.
struct WidgetWorkEvent {
	crl::profile_time started = 0;
	crl::profile_time finished = 0;
	// The display list size a Frame scope was given, else -1.
	int64 displayList = -1;
	// The index in WidgetHostReading::hosts of the attachment that
	// registered the core.
	int host = 0;
	WidgetWorkKind kind = WidgetWorkKind::Message;
	// A Frame scope whose frame() replayed the display list into an image:
	// PaintFrame() ran and returned one. False for a Frame that returned
	// before the replay and for every other kind.
	bool replayed = false;
};

// A watch's work log, as ReadWidgetWork() copies it.
struct WidgetWorkReading {
	// The recorded events that finished at or after the stamp asked for, in
	// the order they finished: `finished` never decreases.
	std::vector<WidgetWorkEvent> events;
	// The last ReserveWidgetWork(): its crl::profile() stamp, 0 before the
	// first, and the events it made room for.
	crl::profile_time reservedAt = 0;
	int capacity = 0;
	// Scopes since that reservation that found the log full (before the
	// first, every scope does): nonzero leaves the log incomplete, so the
	// level that reads it is invalid, whatever `events` holds.
	int64 dropped = 0;
	// The cores of this watch's attachments still registered.
	int registered = 0;
};

// Main thread. Starts |watch|'s work log over for one level: forgets its
// events and drops, allocates room for |capacity| events at once, so no
// scope allocates, and stamps `reservedAt`. The first |capacity| scopes to
// close after it are recorded, every later one only counts in `dropped`.
void ReserveWidgetWork(WidgetHostWatch &watch, int capacity);

// Main thread. A copy of |watch|'s work log, its events limited to those
// whose `finished` is at or after |since|.
[[nodiscard]] WidgetWorkReading ReadWidgetWork(
	const WidgetHostWatch &watch,
	crl::profile_time since = 0);

// The disposable overlay's timing of one RendererCore call, a local at the
// start of the timed part, guarded by the harness condition:
//     const auto workScope = Test::WidgetWorkScope(this, kind);
// (non-const in frame(), which also gives displayList() and replayed()).
// It records one event from its construction to its destruction into the
// log of the watch whose WatchWidgetHost() attachment registered |core|,
// unless another scope of that watch is open around it, whose event
// already holds its time. For any other core it only compares |core| with
// the registered addresses; |core| is never dereferenced. Main thread only.
// No retained product code creates one.
class WidgetWorkScope final {
public:
	WidgetWorkScope(const void *core, WidgetWorkKind kind);
	WidgetWorkScope(const WidgetWorkScope &other) = delete;
	WidgetWorkScope &operator=(const WidgetWorkScope &other) = delete;
	~WidgetWorkScope();

	// A Frame scope's display list, given right after paint() returned it.
	void displayList(size_t size);
	// A Frame scope's replay, given right after PaintFrame() returned:
	// whether its image is not null. Not given: the frame was not replayed.
	void replayed(bool image);

private:
	std::shared_ptr<WidgetWorkLog> _log;
	crl::profile_time _started = 0;
	int64 _displayList = -1;
	int _host = 0;
	WidgetWorkKind _kind = WidgetWorkKind::Message;
	bool _replayed = false;

};

} // namespace Test
