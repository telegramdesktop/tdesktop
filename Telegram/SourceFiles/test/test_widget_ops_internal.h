/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_widget_ops.h"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

// The widget ops tap's shared internals, used by its three translation
// units: test_widget_ops.cpp (the record, the scan, the wrapping factory and
// the arming), test_widget_ops_hosted.cpp (the hosted tap, its registry and
// seams, the effective theme and the helper's work) and
// test_widget_ops_check.cpp (the self-checks). Internal to them; reference:
// docs/helpers/test_widget_ops.md.

namespace Test {

struct HelperWorkRecord;

} // namespace Test

namespace Test::WidgetOps {

inline constexpr auto kMaxExamples = 64;
inline constexpr auto kGapBytes = 160;
inline constexpr auto kDetailBytes = 240;

// The bytes the leak or note examples of one scanned message may have
// together, each example at most a place of kDetailBytes and 200 more.
inline constexpr auto kMaxSampleBytes = std::size_t(64) << 10;

// What the tap asks a hosted widget for its effective theme, and the bytes
// a kept answer may have.
inline constexpr auto kEffectiveProbe = "JSON.stringify("
	"[TelegramWidget.colorScheme, TelegramWidget.themeParams])";
inline constexpr auto kMaxEffectiveBytes = std::size_t(64) << 10;

// The JSON values (keys counted too) one observed message may hold: over
// three times the stress kit's largest validated batch (style-churn's 8001
// ops, about 72000 values). A larger message is a scan gap and is never
// parsed, so no message can make the tap build a tree past about 30 MiB.
inline constexpr auto kMaxScanValues = std::size_t(1) << 18;

// The bytes a kept result value, theme message or helper scheme may have;
// a longer one is a scan gap and is not kept.
inline constexpr auto kMaxKeptValueBytes = std::size_t(256) << 10;
inline constexpr auto kMaxSchemeBytes = std::size_t(64);

// WidgetHost's own wrapping of a validated batch for its renderer.
inline constexpr auto kOpsPrefix
	= std::string_view("{\"type\":\"ops\",\"ops\":");
inline constexpr auto kOpsSuffix = std::string_view("}");

#ifdef _DEBUG
inline constexpr auto kHelperMirrors = true;
#else // _DEBUG
// A Release build with the harness starts a Release helper: it has no tap
// and stops a route at a Mirror control.
inline constexpr auto kHelperMirrors = false;
#endif // _DEBUG

// What a hosted widget answered kEffectiveProbe with, read outside the
// tap's lock: its scheme, the JSON of its palette, the Release theme
// message they give and the first difference from the theme it was probed
// for; or, in `missed`, why the answer is no reading.
struct EffectiveReading {
	std::string scheme;
	std::string params;
	std::string message;
	std::string mismatch;
	std::string missed;
};

// Keyed by session too: local message ids are allocated per session.
using ArmedMap = std::map<GlobalMsgId, std::shared_ptr<WidgetOpsTap>>;

// Main thread.
[[nodiscard]] ArmedMap &ArmedTaps();

// Counts the values |json| would parse into, keys included, without
// allocating: each token that starts a value (a string, number, literal,
// array or object) right after the start, `[`, `{`, `,` or `:`. A token in
// no such place makes the parse fail before it allocates for it, so the
// count bounds the tree whatever the text.
[[nodiscard]] bool WithinScanBudget(std::string_view json);

// steady_clock's now, in microseconds: the clock of a helper's work
// records.
[[nodiscard]] crl::profile_time SteadyMicros();

// What a widget answered kEffectiveProbe with, for |theme|, the theme it
// was probed after: a [scheme, palette] pair within kMaxEffectiveBytes, or
// no reading (an error the probe gave, a throw, anything else).
[[nodiscard]] EffectiveReading ReadEffective(
	std::string_view answer,
	const twidget::WidgetTheme &theme);

// The self-check's hold of a hosted host's theme probes: what the host's
// tap asks the widget with (WidgetHost::probe(kEffectiveProbe)) becomes
// |probe| for that host alone, until an empty |probe| restores it. Main
// thread, while no probe of that host runs. False when |host| has no
// installed tap.
bool ReplaceEffectiveProbe(
	twidget::WidgetHost &host,
	Fn<std::string()> probe);

// WidgetPressDispatched() for a work no hosted tap was installed for: the
// tap that TapWidgetRenderer() registered |work| for, if any, gets the
// dispatch as a hosted one's does. Main thread.
void LocalPressDispatched(uint64 work, uint64 serial);

// WidgetCoreDetached() for a work no hosted tap was installed for: the
// tap that TapWidgetRenderer() registered |work| for, if any, revokes the
// mark bound to it. Main thread.
void LocalDetached(uint64 work);

// |record| of the host's |generation| on Telegram's profile clock: its
// stamps moved by |profile| - |steady|, the two clocks read together at
// its receipt; nullopt when it finished after |steady| (the helper reads
// the same steady clock before it sends a record, so a record that ends
// after its own receipt is malformed, however short and ordered), or when
// that offset or a moved stamp does not fit.
[[nodiscard]] std::optional<WidgetHelperWorkEvent> HelperWorkEvent(
	const HelperWorkRecord &record,
	crl::profile_time profile,
	crl::profile_time steady,
	int generation);

} // namespace Test::WidgetOps

namespace Test {

class WidgetOpsTap final {
public:
	// Returns the selection's 1-based index, its host's generation.
	[[nodiscard]] int factorySelected();
	void factoryCompleted(bool threw);
	void factoryReleased();
	void wrapperBuilt(int selection, const std::string &theme);
	void wrapperDestroyed();
	// A hosted host's observers installed: a build without a scheme.
	void hostedBuilt();
	// The scheme a hosted host's renderer was built with, as reported.
	void schemeReported(int selection, const std::string &scheme);

	// Parse and scan outside the lock, merge under it; never call out.
	void hostMessage(int selection, std::string_view json);
	// A served host's message, mirrored: its `frozen` is not a death here,
	// the hosted host's own death is (hostDied()).
	void servedMessage(int selection, std::string_view json);
	void themeSet(int selection, const std::string &theme);
	// Its trusted events count only for the marked generation, once marked.
	// |work| is the local work whose wrapper forwards it to the host (0 for
	// none, or a mirrored message): a trusted click there during the
	// forwarding of the played press's own release completes the mark.
	void rendererMessage(
		int selection,
		std::string_view json,
		uint64 work = 0);
	void hostDied();
	// A hosted host's own rendered acknowledgment, once per host.
	void hostRendered();
	// The theme the runtime wants a hosted host to have while its theme job
	// runs, by the message it gives: pending from now on, as that job's
	// application when the job gives it (none when that was recorded), else
	// as the application of a job still to start.
	void themeSelected(int selection, std::string message);
	// A theme application the runtime scheduled for a hosted host, with the
	// theme message it gives: pending from now on, its token returned (the
	// selected one when it was pending for that message; 0 for an older
	// host's or a retired one's).
	[[nodiscard]] uint64 themeScheduled(int selection, std::string message);
	// The theme the runtime applied to a hosted host, the one it was built
	// with (|token| 0) or a later one: its serial in the generation, 0 when
	// not kept; ends the pending application when |token| and |message|
	// are that application's.
	[[nodiscard]] int themeApplied(
		int selection,
		std::string scheme,
		std::string message,
		uint64 token = 0);
	// What the widget read after the theme of |serial| was applied: kept
	// only while that is the newest applied theme and its host was not
	// retired.
	void themeEffective(
		int selection,
		int serial,
		WidgetOps::EffectiveReading seen);
	// The runtime retired a hosted host of |selection| with |work|: nothing
	// it publishes later is kept, and a mark bound to that work whose click
	// was not decided yet is revoked (`markDetached`), also for a step a
	// lookup that held the registration before this call reports later.
	void hostRetired(int selection, uint64 work);
	// A primary press the chat dispatched to |work| of a host of
	// |selection|, hosted or local, with the pointer |serial|: the first
	// inside the mark's press delivery, to the marked generation, is the
	// played one.
	void pressDispatched(int selection, uint64 work, uint64 serial);
	// The mark's press delivery begins (|open|) or returns: once it returned
	// the mark is sealed, bound or not, until the next mark.
	void pressDelivery(bool open);
	// A step of a hosted host's primary |press| on |work|: counted only for
	// the marked interaction (the press the mark's dispatch played, sent
	// with its serial) while the marked generation is the newest.
	void pressObserved(
		int selection,
		uint64 work,
		uint64 press,
		const Tgw::PressStep &step);
	// The local renderer of |work| of |selection| took the primary down the
	// chat dispatched with |serial|: the marked press's down when that is
	// the played press, and in any case the press its work's next release
	// ends.
	void localPressDown(int selection, uint64 work, uint64 serial);
	// That release, |clicked| when the renderer answered it with a trusted
	// `click` event: the marked press's up only when the press it ends is
	// the played one, its down taken since the mark while the marked
	// generation is the newest; its click then counts once it reaches the
	// host through the work's wrapper (rendererMessage()) before the
	// release's forwarding ends.
	void localPressReleased(int selection, uint64 work, bool clicked);
	// The release of |work| was forwarded: a click of it that did not reach
	// the wrapper by now never counts.
	void localPressForwarded(uint64 work);
	// The core of |work| of |selection|, hosted or local, is being
	// detached, its host link about to close: a mark bound to that work
	// whose click was not completed yet is revoked (`markDetached`), also
	// for a step a lookup that held the registration before this call
	// reports later.
	void detached(int selection, uint64 work);
	// An observation not taken: the reason and the start of the message.
	void gap(std::string example);

	void markEvents();
	[[nodiscard]] WidgetOpsReading read() const;
	[[nodiscard]] WidgetOpsLatest latest() const;

	void askWork();
	[[nodiscard]] bool asksWork() const;
	void reserveWork(int capacity);
	void helperWork(WidgetHelperWorkEvent event);
	void helperWorkMalformed();
	[[nodiscard]] WidgetHelperWorkReading work(crl::profile_time since) const;

private:
	void merge(int selection, std::string_view json, bool frozen);
	// Under the lock: whether |selection| is the newest host's.
	[[nodiscard]] bool newest(int selection) const;
	// Under the lock: false, counted as stale, for an older host's wrapper.
	[[nodiscard]] bool current(int selection);
	// Under the lock: whether |work| of |selection| is the work the mark's
	// press was played to, while the marked generation is the newest and
	// that work's core was not detached (its hosted host not retired)
	// before its click.
	[[nodiscard]] bool marks(int selection, uint64 work) const;

	// Where the mark's press binding is: awaiting its delivery, open while
	// DeliverMarkedPress() delivers it, sealed once that returned.
	enum class MarkPress : uchar {
		Awaiting,
		Open,
		Sealed,
	};

	mutable std::mutex _mutex;
	WidgetOpsReading _reading;
	WidgetOpsLatest _latest;
	WidgetHelperWorkReading _work;
	// The theme message of the pending application, `themePending`'s.
	std::string _pendingMessage;
	// The newest application the runtime started on the newest host, the
	// constructor's (token 0) or a job's: the theme message it gives, its
	// token and whether it was recorded.
	std::string _startedMessage;
	uint64 _startedToken = 0;
	uint64 _themeTokens = 0;
	// The pointer serial of the down the marked work's local renderer took
	// last since the mark, until a release ends that press.
	uint64 _localDown = 0;
	// The marked work while its renderer forwards the played press's own
	// release, answered with a trusted click that has not reached the host
	// yet; 0 otherwise.
	uint64 _localRelease = 0;
	bool _startedRecorded = false;
	MarkPress _markPress = MarkPress::Sealed;

};

} // namespace Test
