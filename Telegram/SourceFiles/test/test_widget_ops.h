/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <crl/crl_time.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The validated-ops tap of a widget message's sandbox host, hosted in the
// widget helper: what the helper's validator passed and its served host
// and renderer exchanged, scanned with widget-sandbox's --attacks leak
// definition and read for the conformance and stress results, and the
// helper's own renderer work when asked. The helper's half is twidget's
// Rust tap (rust/crates/helper/src/tap.rs); the helper work record it
// writes is read with test_widget_renderer_forward.h.
// Reference: docs/helpers/test_widget_ops.md.

namespace twidget {
class WidgetHost;
struct WidgetTheme;
} // namespace twidget

namespace Tgw {
enum class HostedOutcome : uchar;
struct HostedDecision;
struct PressStep;
} // namespace Tgw

namespace Test {

// The tap of a host whose engine, validator and renderer run in the widget
// helper: Tgw::Runtime::construct() calls it for the message |id|, its
// HistoryItem::globalId(), the FullMsgId and its session's uniqueId(),
// because each session allocates local message ids on its own and two
// sessions can hold different messages with the same FullMsgId, on the
// main thread, once per construction, with |work|, the
// RendererCore::workId() of the core it builds the host for (counted up
// from 1 and never reused, so a later core never shares it; 0 for none),
// and |theme|, the theme the host's Job carries into its constructor.
// Returns an empty function unless Active() and |id| is armed by
// ArmWidgetOpsTap(). For an armed id it selects the tap's next generation
// at once and returns a function for Build()'s worker to call once with
// the hosted WidgetHost, right after its constructor returned and the
// runtime set its observers, before the host is bound or started. That
// call records |theme| as the theme the
// runtime applied to the generation (WidgetOpsLatest's `applied...`
// fields, its first theme serial), registers the host for
// WidgetThemeObservation(), WidgetThemeSelected(), WidgetThemeScheduled(),
// WidgetThemeApplied() and WidgetHostRetired() and |work| for
// WidgetPressDispatched() and WidgetPressObserved(), and
// wraps the host's onRenderOps (the validated batches, which only an
// observer set before start() gets), onPlatformMessage, and onChange (to
// see the host's own rendered acknowledgment and the first death it
// reports, WidgetDiagnostics::dead, kept raw in the selection's
// WidgetHostedRun, never counted), each calling the observer it wrapped,
// if any, after it recorded: onPlatformMessage consumes a well-formed
// Mirrored record of the helper's tap once it asked for them and passes
// every other message on unchanged. The host's onDeath stays the
// runtime's own: a death the host reports is no charge. What the widget
// itself took of |theme| (the `effective...` fields of WidgetOpsLatest, for
// that first serial) is probed only by the function
// WidgetThemeObservation() gives the runtime. In Debug it then asks with
// one Mirror control through sendPlatform(),
// MirrorWork instead when TimeWidgetHelperWork() asked; a Release build
// with the harness has a Release helper, which has no tap, so it asks
// nothing and records the batches, the applied and effective themes, the
// acknowledged presses, the rendered acknowledgment and the death alone.
// A second call does nothing. The function's last copy gone,
// called or not, releases the generation, and one never called also
// completes it (a failed or discarded construction). The installed
// observers end the generation's observations, and both registrations,
// when they are destroyed with the host, also when the helper kept its
// renderer. A registration is erased only by the observations that made it,
// so whichever of two constructions installs first, each keeps its own.
// Every selection starts its WidgetHostedRun. With |finish|, which
// Tgw::Runtime::construct() passes for every hosted construction and a
// check that only installs leaves out, an armed id also sets *|finish| to
// the selection's final observation and counts one more hosted work the
// tap awaits (`hostedSelected`): the runtime keeps it with the
// construction's run, takes it once and calls it on the main thread from
// the run's final accounting with the run's decision, installed or not,
// after the charge was applied and when nothing can be charged to the run
// any more. That call counts the decision once (`hostedFinal` and one of
// `fatalFrozen`, `desktopCharges`, `sparedRuns`, `unaffectedRuns`) and
// keeps it in the selection's record while the record is kept; a second
// call, of any copy, does nothing. It holds the tap weakly, the selection's
// number and |work|, and nothing of the returned function, the host or its
// observers. An unarmed id leaves *|finish| untouched. Without the harness
// this module defines only this function, returning an empty function and
// leaving *|finish| untouched, and the seams below it:
// WidgetThemeObservation(), WidgetThemeSelected(), WidgetThemeScheduled(),
// WidgetThemeApplied(), WidgetHostRetired(), WidgetPressDispatched(),
// WidgetPressObserved() and WidgetCoreDetached().
[[nodiscard]] auto TapHostedWidget(
	GlobalMsgId id,
	uint64 work,
	const twidget::WidgetTheme &theme,
	Fn<void(const Tgw::HostedDecision &decision)> *finish = nullptr)
-> Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)>;

// Tgw::Runtime::constructed() calls this on the main thread for each host
// it starts: an empty function unless a hosted tap is installed on |host|
// (TapHostedWidget()). Then the function, called with that host on any
// thread but the host's own, probes what the widget took of the theme the
// host was built with (TelegramWidget.colorScheme and themeParams) and
// records it as the generation's effective theme for that first serial; a
// reading made after a later theme was applied is superseded, and one
// that comes back after WidgetHostRetired() is revoked: neither is kept.
// It holds the tap only weakly. The runtime calls it at most once, after
// the widget loaded, as one of the host's jobs that run one at a time on a
// worker (never beside a theme application or a keep of the same host),
// and drops it unrun once a new theme or the host's retirement makes it
// obsolete. Without the harness this module defines this function too,
// returning an empty function.
[[nodiscard]] Fn<void(twidget::WidgetHost &host)> WidgetThemeObservation(
	twidget::WidgetHost &host);

// Tgw::Runtime::syncHostTheme() calls this on the main thread when it
// finds |host|'s theme job still running and so starts none, with |theme|,
// the theme it now wants the host to have (a newer app theme, or the one
// it wanted before); the job that gives it starts only after the running
// one ended, or never when the running one gives it. Does nothing unless
// a hosted tap is installed on |host| (TapHostedWidget()) and the host was
// not retired: then the application that gives |theme| is pending for the
// host's generation from now on, as WidgetThemeScheduled() makes one. It
// is the running job's when that job gives |theme| (none when that job
// already recorded its application), else a new token that
// WidgetThemeScheduled() hands to the job for |theme| when it starts, so
// no application of another theme ends it. Without the harness this
// module defines this function too, doing nothing.
void WidgetThemeSelected(
	twidget::WidgetHost &host,
	const twidget::WidgetTheme &theme);

// Tgw::Runtime::syncHostTheme() calls this on the main thread right before
// it starts the job that gives |host| |theme| (ApplyTheme()), for every
// host it re-themes. Returns 0 unless a hosted tap is installed on |host|
// (TapHostedWidget()): then it records that application as pending for
// the host's generation and returns its token, nonzero and never reused
// by the tap (the one WidgetThemeSelected() made pending for |theme|, if
// it is still pending), which the job hands to WidgetThemeApplied(). From
// then on the generation's theme is unobserved (WidgetThemeUnobserved()),
// whatever it read before, until the job with that token recorded its
// application, and the reading of that application is the next one kept.
// Without the harness this module defines this function too, returning 0.
[[nodiscard]] uint64 WidgetThemeScheduled(
	twidget::WidgetHost &host,
	const twidget::WidgetTheme &theme);

// Tgw's ApplyTheme() calls this on its worker right after it gave |host|
// |theme| behind its barrier (an apply, a probe, an apply again), for every
// host it re-themes, with |token|, what WidgetThemeScheduled() returned
// when the job was scheduled; never on the host's own thread. Does nothing
// unless a hosted tap is installed on |host| (TapHostedWidget()): then it
// records |theme| as the theme the runtime applied to that host's
// generation (its next theme serial; the pending application ends only
// when |token| is the pending one and |theme| its theme, so an older job
// never ends a newer one), probes the widget on this thread for the scheme
// and palette it now reads (TelegramWidget.colorScheme and themeParams)
// and records that as the generation's effective theme for that serial.
// After WidgetHostRetired() it records neither. It takes the tap's
// registry lock and the tap's own, never a runtime lock, and holds none
// while it probes. Without the harness this module defines this function
// too, doing nothing.
void WidgetThemeApplied(
	twidget::WidgetHost &host,
	const twidget::WidgetTheme &theme,
	uint64 token);

// Tgw::Runtime calls this on the main thread when it retires |host|, its
// live host (Runtime::retire(), ~Runtime()), before the host's last jobs
// end. Does nothing unless a hosted tap is installed on |host|: then it
// revokes what the host may still publish to its generation, under the
// tap's lock: a theme application or an effective reading a job of that
// host records after it is not kept (`effectiveError` names the
// retirement), so a probe already running at the retirement cannot make
// the generation's theme observed. Under the same lock it revokes the mark
// bound to the host's work unless its renderer's click for the played
// press was already reported (`markDetached`): the work's detach reaches
// the widget render thread only later, so a reply that thread already
// took can still report the press's up and click to WidgetPressObserved()
// after this call, also through a lookup that held the registration
// before it; such steps count in `pressStepsElsewhere` alone. Readings
// kept before it stay, a completed click included. Without the harness
// this module defines this function too, doing nothing.
void WidgetHostRetired(twidget::WidgetHost &host);

// Tgw::RendererCore::pointerPress() calls this on the main thread for
// every primary mouse press it dispatches to its work, right before it
// posts it, with |work|, its RendererCore::workId(), and |serial|, the
// pointer serial it gave the press (the one the press carries to
// Tgw::HostedInput::press() and WidgetPressObserved()'s Sent). When |work|
// is the work of the marked generation's host with an installed hosted
// tap, and that tap delivers its mark's press (DeliverMarkedPress()), the
// first such press inside that delivery binds the mark to that work and
// serial: the press the fixture played. A press dispatched before or after
// the delivery binds nothing, one the work still had queued or a later one
// of the same work included. Without the harness this module defines this
// function too, doing nothing.
void WidgetPressDispatched(uint64 work, uint64 serial);

// Tgw::HostedInput calls this on the widget render thread, through the
// delegate Tgw::RendererWork::makeHosted() gives it, with |work|, that
// work's id (the RendererCore::workId() its host was tapped with), and
// |press|, the number of a primary mouse press the helper's renderer and
// Telegram both count (Tgw::RendererPlatform::lastDown() at its down), at
// each of its steps (Tgw::PressStep in tgw/tgw_renderer_input.h): Sent when
// its down went out on the live route, with the pointer serial
// RendererCore::pointerPress() dispatched it with, Down and Up when the
// helper's renderer answered its down and the up that ended it while the
// route was still active, Click when the helper then reported (the Clicked
// control) that its renderer decided a click for that up: the press and
// the release landed on one element, or one inside the other, the
// decision the renderer emits its `click` event for whether or not the
// widget listens. Down and Up only prove delivery; an up whose target
// changed since the down reports no Click. A down sent but never answered,
// a press cancelled or taken by the chat, a secondary or finger press and
// a step after the route closed, detached or stopped call nothing. When
// |work| is the work of a host with an installed hosted tap, the steps
// count for that tap's marked interaction (MarkWidgetOpsEvents()) alone.
// Without the harness this module defines this function too, doing
// nothing.
void WidgetPressObserved(
	uint64 work,
	uint64 press,
	const Tgw::PressStep &step);

// Tgw::RendererCore::detach() calls this on the main thread, with |work|,
// its RendererCore::workId(), first, before it closes the core's host link
// and posts the work's detach (Runtime::retire() and ~Runtime() detach a
// live core before dropping, keeping or releasing it and before they
// retire its host): from the close on nothing the work's renderer answers
// reaches the host, while a step the work already took can still run on
// the widget render thread and report to WidgetPressObserved(), and a host
// observer can still hold the registration. Called for every core. When
// |work| is the work of a host with an installed hosted tap and the mark
// is bound to it, the tap revokes the mark at once, under its lock, unless
// the played press's click already completed (`markDetached`): a step of
// that work reported later counts in `pressStepsElsewhere` alone and no
// click of it completes the mark, also when it comes through a lookup that
// held the registration before this call. A click completed before it
// stays; another work's detach changes nothing. Without the harness this
// module defines this function too, doing nothing.
void WidgetCoreDetached(uint64 work);

// Everything below is harness-only: declared here and deliberately left
// undefined in a build without the harness, because no production code
// reads it.

// One armed message's record, shared by the main thread, the host's Build
// worker, its host thread and its deleter worker. Opaque; read it with
// ReadWidgetOps().
class WidgetOpsTap;

// The hosted works a tap keeps a record of: the newest selections.
inline constexpr auto kWidgetHostedRuns = 32;

// One hosted selection of a tap (TapHostedWidget()), its detail for
// attribution: what its host reported of its own death and what the
// runtime's final accounting decided of its run. Each text is cut by
// Tgw::ClipLogLine(), as the "Died: " line of a summary is.
struct WidgetHostedRun {
	// The first death the host reported to its onChange
	// (WidgetDiagnostics::dead), raw, also one it reported after the
	// final accounting or with its onDeath suppressed by its owner's
	// keep or destroy; nullopt while it reported none.
	std::optional<QString> died;
	// The final decision's own reason (Tgw::HostedDecision::reason): the
	// stopped-responding text, an ordinary death's label or Telegram's
	// own refusal; empty for none.
	QString reason;
	// The RendererCore::workId() it was selected with (0 for none) and the
	// selection's index, its generation.
	uint64 work = 0;
	int generation = 0;
	// The final charge, and what the deciding event read of the stream's
	// decision for the route (Tgw::HostedDecision::transport), apart: a
	// refusal on a route the stream spared keeps both. At an engine's own
	// death the channel reads Ordinary as an ordinary end does, so an
	// Ordinary transport is no proof that the stream published an end.
	// Both None (value 0) until `final`.
	Tgw::HostedOutcome outcome = {};
	Tgw::HostedOutcome transport = {};
	// A charge that a claim of Telegram's or of the widget's own engine
	// decided with no end of the stream to read: a refusal whose local
	// claim found none, or a discarded construction's own death.
	bool claimed = false;
	// Whether the runtime's final accounting reached the tap: only a
	// selection given |finish| has one.
	bool final = false;
};

// A snapshot of one tap. Every count covers every renderer built for the
// message while it was armed and is never reset, except the trusted event
// fields and the press readings (`markedGeneration` to `markDetached`),
// which count from the last MarkWidgetOpsEvents().
// Counts are uncapped; every example list keeps
// only its first 64 entries, `hostedRuns` its newest kWidgetHostedRuns.
struct WidgetOpsReading {
	// The --attacks scanner's own leak and note texts, each built only while
	// its message held fewer than 64 within 64 KiB, the place it names cut
	// to 240 bytes first; `leaks` and `notes` count every match.
	std::vector<std::string> leakExamples;
	std::vector<std::string> noteExamples;
	// The reason and the first 160 bytes of each scan-gap message.
	std::vector<std::string> gapExamples;
	// The `detail` of each renderer `violation` message, never a leak.
	std::vector<std::string> violationDetails;
	// The `code` and `count` of each renderer `limit` report, as
	// "paint-limit 17": a string code bare, any other value as JSON text.
	std::vector<std::string> limitExamples;
	// The `ev.type` of each trusted renderer `event` since the mark.
	std::vector<std::string> trustedEventTypes;

	// Hosted taps handed out by TapHostedWidget(), on main.
	int factoriesSelected = 0;
	// Hosted taps installed on their host or released uninstalled; a
	// hosted tap never throws, so `factoriesThrew` stays 0.
	int factoriesCompleted = 0;
	int factoriesThrew = 0;
	// Hosted taps whose last copy is gone, installed or not: none of them
	// can reach a host any more.
	int factoriesReleased = 0;
	// A hosted host's observers count as built when the tap installs them
	// and as destroyed with the host, whether or not its renderer was kept.
	int wrappersBuilt = 0;
	int wrappersDestroyed = 0;

	// Host -> renderer messages, every type: a hosted host's validated
	// batches and the served host's messages its renderer got, as the
	// helper's tap mirrored them.
	int hostMessages = 0;
	// `ops` messages with an `ops` array, each scanned before forwarding,
	// and the ops in them.
	int batches = 0;
	int ops = 0;
	// Observations not taken, each forwarded unchanged: a host message that
	// is not JSON, an `ops` message whose `ops` is not an array, a host or
	// renderer message of more values than the scan budget (never parsed), a
	// result value or theme message too long to keep and a helper scheme
	// record too long. Any gap leaves the reading incomplete.
	int scanGaps = 0;
	int leaks = 0;
	int notes = 0;

	// Renderer -> host `violation` messages.
	int violations = 0;
	// Renderer -> host `limit` reports, each counted before it is forwarded
	// whatever its code and count, also when WidgetHost ignores it (compat,
	// or a widget already dead): two reports count twice.
	int limitReports = 0;
	// Renderer -> host `event` messages marked `trusted`, and those of them
	// whose `ev.type` is "click", since the last MarkWidgetOpsEvents(), of
	// the generation it marked alone while no newer one was selected (of
	// every generation before a mark).
	int trustedEvents = 0;
	int trustedClicks = 0;
	// The marked interaction: the generation the last MarkWidgetOpsEvents()
	// found newest; the work and pointer serial of the first primary press
	// the chat dispatched to a tapped work of that generation while
	// DeliverMarkedPress() delivered the fixture's own mouse-down
	// (WidgetPressDispatched(); 0 when none did, for good: the binding is
	// sealed when that delivery returns), the press the fixture played; and
	// the number that press got when its work sent it (WidgetPressObserved()'s
	// Sent with that serial; 0 before it). Its down and up answered on a
	// route still active (delivery only) and the renderer's click decision
	// for that up reported after it, each counted for that one press alone
	// while the marked generation is the newest and neither its core was
	// detached nor its host retired before the click (`markDetached`), and
	// `clicksCompleted` only after its up. A step of any other press, an
	// earlier one of the same work still queued at the mark, a later one of
	// the same work after an undelivered mark or one of an older generation
	// that completes late included, and every step after a newer generation
	// was selected, the marked work's core was detached or its host retired,
	// counts in `pressStepsElsewhere` alone. A down or up, even both
	// answered, never counts as a click. `clicksCompleted` is the click
	// reading of a hosted host whose helper has no tap (WidgetHelperMirrors()
	// false), where `trustedClicks` stays 0; with the Debug tap
	// `trustedClicks` counts the generation's trusted clicks whatever press
	// made them, and only `clicksCompleted` is the played press's own.
	int markedGeneration = 0;
	uint64 markedWork = 0;
	uint64 markedSerial = 0;
	uint64 markedPress = 0;
	int downsConfirmed = 0;
	int upsConfirmed = 0;
	int clicksCompleted = 0;
	int pressStepsElsewhere = 0;
	// The generation whose selection superseded the marked one, 0 while the
	// marked one is the newest: the mark's later steps and trusted events
	// no longer count, while what it counted before stays.
	int markSuperseded = 0;
	// Whether the core of the work the mark is bound to was detached
	// (WidgetCoreDetached(), before its host link closed), or the host of
	// that work was retired (WidgetHostRetired()), before the played
	// press's click completed: the mark's later steps and its click no
	// longer count, while what it counted before stays.
	bool markDetached = false;

	// The served host's post-load ack (`query` with numeric `qid` -1) and
	// the renderer's answer to it (`result` with numeric `qid` -1), as the
	// helper's tap mirrored them, and the host's deaths (the runtime's
	// final accounting of a selected work charged with an ordinary or
	// stalled death, never a death the host reported by itself, nor the
	// served host's mirrored `frozen`).
	int ackQueries = 0;
	int ackResults = 0;
	int fatalFrozen = 0;
	// Hosted works selected for the runtime's accounting (TapHostedWidget()
	// with |finish|), and those whose final accounting reached the tap, each
	// once, after its charge was applied: the hosted reading is final only
	// when every selected work is. Each final is in `fatalFrozen`, in
	// `desktopCharges` (a refusal Telegram itself made of the widget's own
	// traffic, also on a route the stream spared), in `sparedRuns` (the
	// stream spared the run and nothing charged it) or in `unaffectedRuns`
	// (nothing charged or spared it: withdrawn first, or never part of an
	// end). No eviction of `hostedRuns` takes any of them back.
	int hostedSelected = 0;
	int hostedFinal = 0;
	int desktopCharges = 0;
	int sparedRuns = 0;
	int unaffectedRuns = 0;
	// The newest kWidgetHostedRuns hosted selections, in selection order, for
	// attribution alone: a work's verdict is never read from its absence.
	std::vector<WidgetHostedRun> hostedRuns;
	// Hosted hosts whose own WidgetDiagnostics::rendered the tap saw, each
	// once: the served host's post-load query answered by the helper's
	// renderer, as the hosted client recorded it. The hosted twin of an
	// ackQueries / ackResults pair, and the only one a Release harness has.
	int renderedHosts = 0;
};

// The `data-stress` values one WidgetOpsLatest keeps at most.
inline constexpr auto kWidgetStressValuesLimit = 4096;

// One `data-stress` value of the adopted <html>, with its receipt.
struct WidgetStressValue {
	// The raw attribute text, not checked to be JSON.
	std::string value;
	// crl::profile() (monotonic microseconds, the clock of test_widget_host's
	// stamps) when the host handed the batch carrying it to the tap, on its
	// host thread, before the tap parsed the batch.
	crl::profile_time receivedAt = 0;
};

// What the newest host selected for one tap was last told: its widget's
// results, read as CLIENTS.md's "Reading the ops" does, its batch counts,
// and the scheme and theme its renderer got. Every field but `serial` and
// `staleUpdates` describes the host of `generation` alone and restarts with
// a newer one.
struct WidgetOpsLatest {
	// The last `data-conformance` value of the adopted <html>: set by the
	// adopting `el` op's attribute, replaced by each later `attr` op on that
	// id. Raw attribute text, not checked to be JSON. Empty before the first
	// value, once a newer host is selected, and after a later adoption
	// without one; a `rattr` is ignored, as CLIENTS.md's reader does.
	std::optional<std::string> conformance;
	// Every `data-stress` value of the adopted <html>, read as `conformance`
	// is but all kept, in the order the host sent them: the adopting `el`
	// op's attribute, then each later `attr` op on that id. Holds the first
	// kWidgetStressValuesLimit, within 4 MiB of values together; a later
	// adoption clears it, `stressBytes` and `stressDropped`.
	std::vector<WidgetStressValue> stress;
	// The raw `{"type":"theme",...}` message the served host last sent to
	// its renderer, as the Debug helper's tap mirrored it; without that tap
	// (a Release harness) the widget's own reading alone:
	// `{"type":"theme","theme":{"colorScheme":...,"params":...}}` of the
	// newest effective reading, empty before the first.
	std::string themeMessage;
	// The scheme its renderer was given: empty until the Debug helper's tap
	// reported the scheme its renderer was built with, then each scheme the
	// tap's theme mirror reported; without that tap (a Release harness)
	// `effectiveScheme` of the newest effective reading, never the theme the
	// runtime asked for: the first reading sets it, each later reading that
	// changes it counts in `schemeChanges`.
	std::string scheme;
	// The theme the runtime applied to the host: the one its constructor
	// got (TapHostedWidget()), then each WidgetThemeApplied(), recorded once
	// the application returned. The scheme as the host keeps it ("dark",
	// else "light"), and the `{"type":"theme",...}` message that theme gives
	// the helper's renderer, built with the SDK's normalization as
	// WidgetHost::themeMessage() builds it (the checked palette under its 15
	// keys, the button and text tokens): the expectation, never a reading of
	// the widget.
	std::string appliedScheme;
	std::string appliedThemeMessage;
	// What the widget itself read after the runtime
	// applied a theme, probed in it (TelegramWidget.colorScheme, and the JSON
	// of TelegramWidget.themeParams): for the constructor's theme at the
	// host's first rendered acknowledgment, for a later one right after
	// WidgetThemeApplied()'s application. Kept only for the newest applied
	// theme, its serial in `effectiveSerial`; a reading longer than 64 KiB is
	// not kept. Empty before the first.
	std::string effectiveScheme;
	std::string effectiveParams;
	// Empty when the kept reading has the scheme of the theme it was probed
	// for and, for each key of that theme's checked palette, its value (the
	// widget completes the rest from the scheme's defaults); else the first
	// difference.
	std::string effectiveMismatch;
	// The last probe that kept no reading: a host already gone, a widget
	// that answered an error or no reading, a reading past 64 KiB, one for
	// a theme a newer one superseded, or one (or an application) its host
	// published after it was retired; its reason and the start of the
	// answer.
	std::string effectiveError;
	// The id of the adopting `el` op, while `adopted`.
	int64 htmlId = 0;
	// This host's `ops` messages with an `ops` array, each counted whatever
	// it carries, result or not; their ops, and the ops of the largest one
	// (CLIENTS.md's maxBatchOps since the host was created).
	int64 ops = 0;
	int batches = 0;
	int maxBatchOps = 0;
	// The bytes of the `stress` values kept.
	int64 stressBytes = 0;
	// `data-stress` values that found `stress` full: nonzero means `stress`
	// stopped early and is incomplete.
	int stressDropped = 0;
	// The 1-based count of hosted taps selected for the message when this
	// host's was: above 1, more than one host was constructed for it,
	// and these fields, like a runtime's log, cover only the last.
	int generation = 0;
	// `data-conformance` values this host published, the probe fixture's
	// `updates`.
	int updates = 0;
	// Advances whenever `conformance` changes, a clear included; never reset.
	// A `data-stress` value never moves it.
	int serial = 0;
	// Later scheme readings of this host that changed `scheme`: the Debug
	// helper's theme mirror, else the widget's effective readings.
	int schemeChanges = 0;
	// Themes the runtime applied to this hosted host, its construction's
	// included: the serial of the newest.
	int appliedThemes = 0;
	// Effective readings kept for this hosted host, the `appliedThemes`
	// serial the newest was probed for, and probes that kept none
	// (`effectiveError`).
	int effectiveThemes = 0;
	int effectiveSerial = 0;
	int effectiveMissed = 0;
	// Of a hosted host, the token of the application of the theme the
	// runtime wants it to have and has not yet recorded, from the moment the
	// runtime selected that theme (WidgetThemeSelected() while a job runs,
	// else WidgetThemeScheduled()), 0 when none is underway: while it is set
	// the host may already read part of the new theme or still be given an
	// older one, and no reading certifies either.
	uint64 themePending = 0;
	// Of a hosted host, whether the runtime retired it
	// (WidgetHostRetired()): nothing it publishes later is kept.
	bool hostRetired = false;
	// Host messages with an adoption, a `data-conformance` or `data-stress`
	// attribute or a theme, and an older host's scheme reports and theme
	// records after a newer host was selected: ignored above, counted here;
	// never reset. An older host's batches count in WidgetOpsReading alone,
	// never in `batches`, `ops` or `maxBatchOps`, and never here unless they
	// carry such an op.
	int staleUpdates = 0;
	bool adopted = false;
};

// Main thread. Arms |id|, the session-qualified id the message's
// HistoryItem::globalId() will return, for every host constructed for it
// from now on and returns its tap; arming an armed id returns the tap it
// already has. A message of another session with the same FullMsgId stays
// unarmed, and gets a tap of its own only when its own id is armed.
[[nodiscard]] std::shared_ptr<WidgetOpsTap> ArmWidgetOpsTap(GlobalMsgId id);

// Main thread. Later constructions for |id| get no tap; hosted taps already
// handed out and the observations they installed keep the tap.
// Pass the id that was armed, as kept by the caller, not one rebuilt later
// from whichever session is active then; no other id is touched.
void DisarmWidgetOpsTap(GlobalMsgId id);

// Any thread. Only a reading for which WidgetTapFinished() holds is final.
[[nodiscard]] WidgetOpsReading ReadWidgetOps(const WidgetOpsTap &tap);

// Whether |reading| is final: no hosted tap and no host observation of its
// tap is pending (`factoriesReleased == factoriesSelected` and
// `wrappersDestroyed == wrappersBuilt`) and every hosted work selected for
// the runtime's accounting had its final (`hostedFinal == hostedSelected`),
// its charges included. Reads these counts alone: the live-chat fixture's
// removal barrier (FixtureBarrierDecided() of test_widget_fixture.h, which
// owns the removal, its bound and its WidgetTapCounts() diagnostic), both
// evidence packs and the tap's own lifecycle checks share it.
[[nodiscard]] bool WidgetTapFinished(const WidgetOpsReading &reading);

// Any thread. A snapshot taken under the tap's lock, `stress` copied whole;
// a caller that parses `conformance` can cache the result by `generation`
// and `serial`.
[[nodiscard]] WidgetOpsLatest ReadWidgetOpsLatest(const WidgetOpsTap &tap);

// What a hosted message's reading says of its widget's death, as the
// conformance runner and the attack audit both report it.
struct WidgetHostedDeath {
	// The sampled summary's "Died: " line, verbatim, empty for none: never
	// a verdict by itself, since every host of an ended helper prints it.
	QString deathLine;
	// The measured cause, empty unless |measured|.
	QString reason;
	// The sampled work's record, when the reading kept it.
	std::optional<WidgetHostedRun> sampled;
	// The work the summary was sampled from (WidgetRuntimeLog::summaryWork),
	// 0 for none.
	uint64 work = 0;
	// Whether any selected work of the reading was charged.
	bool died = false;
	// Whether |reason| was measured from the sampled work's own
	// attribution.
	bool measured = false;
};

// The one hosted reading of a death: |reading|, the summary sampled from
// |work| and that summary's raw |deathLine|. The verdict is whether any
// selected work was charged (`fatalFrozen` or `desktopCharges` positive,
// or the sampled work's record final with a charge); read from the final
// reading (WidgetTapFinished()) it is final, and no eviction of a record
// reverses it. Only the sampled work's own record gives a cause, only once
// it is final and charged: a Stalled run and a refusal Telegram made give
// the reason recorded for them, also when the sample was healthy or shows
// the end of a helper that spared it; an Ordinary run gives that host's
// own raw diagnosis, the reason of |deathLine|, else the record's `died`,
// unless it is empty or "terminated". A charge without a cause so
// attributable stays dead with the reason unmeasured, and a spared or
// unaffected sample never takes another work's reason. |earlier|, a
// reading of the same nonzero |work| made while its record was kept, gives
// its measured reason once the record is gone from |reading|: a final
// charge never changes. Another generation's reason is never used.
[[nodiscard]] WidgetHostedDeath ReadHostedDeath(
	const WidgetOpsReading &reading,
	uint64 work,
	const QString &deathLine,
	const WidgetHostedDeath *earlier = nullptr);

// What both packs report of |death| as its reason: the measured cause, else
// "charged, reason unmeasured" for a death whose cause the sampled work's
// own record does not give, empty while nothing was charged.
[[nodiscard]] QString WidgetHostedDeathText(const WidgetHostedDeath &death);

// Whether the cumulative counts of |reading| hold a death of its widget: a
// hosted work's final charge (`fatalFrozen` or `desktopCharges`). A raw
// death a host reported is never one, so a wait that ends at a death ends
// at no other host's line.
[[nodiscard]] bool WidgetTapCharged(const WidgetOpsReading &reading);

// Whether |reading|'s tap selected a hosted work for the runtime's
// accounting (`hostedSelected` positive): the widget runs in the widget
// helper, and its death is read by ReadHostedDeath(). Read from the
// message's own selections, never from the runtime's engine placement,
// which no construction may have chosen yet: a reading with no selection
// at all is not hosted, and holds no charge.
[[nodiscard]] bool WidgetTapHosted(const WidgetOpsReading &reading);

// "none", "spared", "ordinary", "stalled" or "refused", for rows.
[[nodiscard]] QString WidgetHostedOutcomeText(Tgw::HostedOutcome outcome);

// |run| in one line for rows: "<generation>:<outcome>/<transport>", then
// "/claimed" for a claimed charge and "/open" before its final, its
// work, its reason and the raw death its host reported ("-" for none).
[[nodiscard]] QString WidgetHostedRunText(const WidgetHostedRun &run);

// A hosted construction a self-check plays as Tgw::Runtime::construct()
// makes one, on the armed |tap| of |id| for |work|: selected with the
// final observation the runtime keeps (TapHostedWidget() with |finish|),
// installed or not on a stub host of its own (no channel, never started,
// its onDeath standing for the runtime's own), whose onChange then reports
// a raw death as a host of an ended helper does, finalized once and
// dropped. selected(), install() and drop() read the tap around their
// step, so a play whose selection, installation or release did not happen
// says so instead of leaving a check to pass on literal readings alone.
// Main thread; copies of a play would hold its installer, so it only
// moves.
class WidgetHostedPlay final {
public:
	WidgetHostedPlay(
		std::shared_ptr<WidgetOpsTap> tap,
		GlobalMsgId id,
		uint64 work);
	WidgetHostedPlay(WidgetHostedPlay &&other) = default;
	WidgetHostedPlay &operator=(WidgetHostedPlay &&other) = default;

	[[nodiscard]] uint64 work() const;
	// The selection gave both the installer and the final observation, and
	// the tap counted one more hosted work it awaits.
	[[nodiscard]] bool selected() const;
	// Builds the stub host and installs the tap on it: true only when the
	// tap counted one more wrapper for it.
	[[nodiscard]] bool install();
	// The installed host's onChange with a death of |dead| (none for null)
	// and, with |rendered|, its rendered acknowledgment: false with no
	// installed host.
	bool change(const char *dead, bool rendered = false);
	// The installed host's onDeath with |reason|: false with no installed
	// host.
	bool death(const char *reason);
	// What the host's own onDeath, the runtime's stand-in, got, in order.
	[[nodiscard]] std::vector<std::string> deaths() const;
	// The tap's record of the work now, if it keeps one.
	[[nodiscard]] std::optional<WidgetHostedRun> record() const;
	// A copy of the final observation, as a second holder would keep it,
	// empty once finish() took it.
	[[nodiscard]] auto observation() const
	-> Fn<void(const Tgw::HostedDecision &decision)>;
	// Takes the final observation and calls it with |decision|, as
	// Tgw::Runtime's final accounting calls it: false when it was taken
	// before or never given.
	bool finish(const Tgw::HostedDecision &decision);
	// Releases the installer and destroys the host, as the runtime drops
	// them once the construction returned: true only when the tap then
	// counted the selection released and, once installed, its wrapper
	// destroyed.
	[[nodiscard]] bool drop();

private:
	std::shared_ptr<WidgetOpsTap> _tap;
	Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> _install;
	Fn<void(const Tgw::HostedDecision &decision)> _finish;
	std::shared_ptr<twidget::WidgetHost> _host;
	std::shared_ptr<std::vector<std::string>> _deaths;
	uint64 _work = 0;
	bool _selected = false;
	bool _installed = false;

};

// Of a hosted host's |latest|: empty when the widget's effective theme was
// read for the newest theme the runtime applied to it (`effectiveSerial ==
// appliedThemes`, at least one applied) and no newer application is
// underway (`themePending`); else "theme-pending" while one is, and
// "theme-unobserved" otherwise, with why. No reading yet, a closed,
// delayed, superseded or revoked probe and a reading not kept all leave it
// unobserved. A kept reading can still differ from the theme
// (`effectiveMismatch`).
[[nodiscard]] std::string WidgetThemeUnobserved(const WidgetOpsLatest &latest);

enum class WidgetThemeVerdict : uchar {
	Matches,
	Unobserved,
	Wrong,
};

struct WidgetThemeCheck {
	WidgetThemeVerdict verdict = WidgetThemeVerdict::Unobserved;
	// Empty for Matches; else why, in words a row can carry.
	std::string detail;
};

// The one predicate every hosted reader of a widget's theme applies: the
// newest generation's effective reading at its newest applied serial with
// no application underway (no reading, a closed, delayed, superseded or
// revoked probe and a pending application are Unobserved, as
// WidgetThemeUnobserved() says), then the scheme the runtime applied
// (`appliedScheme`, as a host keeps it) against |scheme| and the reading
// against that applied theme (`effectiveMismatch`: its scheme, then every
// key of its SDK-checked palette, so a wrong background with the right
// scheme is Wrong too). With |before|, a reading taken before a switch,
// only a fresh application counts: the same generation and a newer applied
// serial, else Unobserved (superseded by a newer host, or no new theme
// applied). An application request, an app theme switch or an earlier good
// reading never makes it Matches.
[[nodiscard]] WidgetThemeCheck CheckWidgetTheme(
	const WidgetOpsLatest &latest,
	std::string_view scheme,
	const WidgetOpsLatest *before = nullptr);

// Main thread, right before a click. Restarts the trusted event fields and
// the press readings, and marks the newest generation as the interaction
// they read; its press is bound only inside DeliverMarkedPress(). A newer
// generation selected before the interaction completes supersedes the
// mark.
void MarkWidgetOpsEvents(WidgetOpsTap &tap);

// Main thread, once after MarkWidgetOpsEvents(). Calls |deliver|, the
// synchronous delivery of the fixture's own mouse-down (a sendEvent() that
// returns before any posted event or postponed call runs), with the mark's
// press binding open, and seals it when |deliver| returns or throws. The
// first primary press the chat dispatches inside that call to a tapped
// work of the marked generation
// (WidgetPressDispatched()), binds the mark to its work and pointer
// serial, whose later steps go on counting as the marked
// interaction's. A mark still unbound at the seal stays unbound
// until the next mark, so no press dispatched later, of the same work
// included, can complete it. A second call, or one after the mark was
// superseded, only calls |deliver|.
void DeliverMarkedPress(WidgetOpsTap &tap, Fn<void()> deliver);

// Whether this build's widget helper has the Debug tap that mirrors a
// hosted host's renderer messages, scheme and theme message
// (docs/helpers/test_widget_helper_tap.md): true in Debug. False in a
// Release build with the harness, whose hosted readings of the theme are
// the widget's effective ones and whose hosted click reading is
// `clicksCompleted`.
[[nodiscard]] bool WidgetHelperMirrors();

// What a helper renderer operation was: test_widget_renderer_forward.h's
// HelperWorkKind, value for value.
enum class WidgetHelperWorkKind : uint8 {
	Message = 1,
	Theme = 2,
	Tick = 3,
	Paint = 4,
	Platform = 5,
	Control = 6,
};

// One outermost renderer operation of a hosted host's helper, as its
// Debug tap timed it on the served host's glue thread.
struct WidgetHelperWorkEvent {
	// crl::profile() stamps, mapped from the helper's steady clock (on macOS
	// the system-wide uptime clock both processes read) at the record's
	// receipt: one clock with test_widget_host's work events. A record whose
	// stamps are not ordered, span more than an hour or do not map within
	// the clock's range is malformed, never kept, so `finished - started`
	// of a kept event is exact, 0 to an hour.
	crl::profile_time started = 0;
	crl::profile_time finished = 0;
	// Microseconds of the interval the helper spent waiting for Telegram's
	// answers to its backend requests, and how many: Telegram's work for
	// them (its render thread's answers) overlaps this part, so a total of
	// both processes subtracts it once.
	crl::profile_time waited = 0;
	// The FrameRequest's seq for Tick and Paint, else 0.
	uint64 seq = 0;
	std::uint32_t waits = 0;
	// Bytes of the Frame messages the operation sent, 0 for none.
	std::uint32_t frameBytes = 0;
	// The tap generation of the host whose helper reported it.
	int generation = 0;
	WidgetHelperWorkKind kind = WidgetHelperWorkKind::Message;
};

struct WidgetHelperWorkReading {
	// The recorded events that finished at or after the stamp asked for, in
	// the order they arrived.
	std::vector<WidgetHelperWorkEvent> events;
	// The last ReserveWidgetHelperWork(), 0 before the first, and the room it
	// made.
	crl::profile_time reservedAt = 0;
	int capacity = 0;
	// Records since that reservation that found the room full or did not
	// decode (those also in `malformed`): nonzero leaves the reading
	// incomplete.
	int64 dropped = 0;
	int malformed = 0;
	// Whether TimeWidgetHelperWork() asked for them.
	bool asked = false;
};

// Main thread, after arming and before the message's host is constructed:
// a hosted host constructed for |tap| from now on asks its Debug helper for
// its renderer work (the MirrorWork control). Nothing arrives from a Release
// helper, or for a host built before the call: the unarmed control.
void TimeWidgetHelperWork(WidgetOpsTap &tap);

// Main thread. Starts |tap|'s helper work over: forgets its events and
// drops and makes room for |capacity| events at once; later records past it
// only count in `dropped`.
void ReserveWidgetHelperWork(WidgetOpsTap &tap, int capacity);

// Any thread. A copy taken under the tap's lock.
[[nodiscard]] WidgetHelperWorkReading ReadWidgetHelperWork(
	const WidgetOpsTap &tap,
	crl::profile_time since = 0);

// |a| + |b|, or nullopt when the sum does not fit: how the tap maps helper
// stamps to its clock and how a reader of the events sums their times.
[[nodiscard]] std::optional<crl::profile_time> CheckedTimeSum(
	crl::profile_time a,
	crl::profile_time b);

struct WidgetOpsSelfCheckRow {
	QString name;
	QString details;
	bool passed = false;
};

struct WidgetOpsSelfCheck {
	std::vector<WidgetOpsSelfCheckRow> rows;
	// The hosted leg's tap's final reading, after its host is gone.
	WidgetOpsReading control;
};

// Where RunWidgetOpsSelfCheck() plays its exchange leg, whose streams stay
// open past the hosted answer deadline, about 5 s in all: within the call,
// or apart, on a WidgetOpsExchanges of the caller's.
enum class WidgetOpsExchangeLeg {
	Within,
	Apart,
};

// Main thread, synchronous, needs Active(). Drives TapHostedWidget() under
// message ids of peer 0, which no real message has in any session, on
// hosted WidgetHosts built without a channel and never started, calling
// the observers the tap installed directly: no tap unarmed, selection at
// once, one install, release by the last copy, the scanner's 2-leak,
// 1-note control batch scanned and passed on, each mirrored way recorded
// (in Debug; without a Debug helper every message passes on), a malformed
// mirror passed on, a served frozen no death, every onDeath reaching the
// runtime's own observer and counting nothing (one reading terminated
// too), the first death a change carries kept raw in the selection's
// record and a later one changing nothing, the host's rendered
// acknowledgment counted once and its one theme probe (a widget with no
// runtime: no reading, the theme unobserved), helper work records kept
// within their room, dropped past it, malformed ones (unordered, over an
// hour, negative, an overwaited
// interval) counted, full 32-bit counts kept and a stamp that maps past
// the clock's range refused, the construction's and a later applied theme
// recorded as the expectation (never as the renderer's scheme) while
// another host's theme is not, the effective readings (a superseded, an
// oversized and a malformed one not kept, a mismatching one kept with its
// difference, a matching one, and without a Debug helper the scheme, its
// changes and the theme message they give), a theme selected while another
// theme's job runs pending from its selection until its own job applied
// and read it, downs and completed clicks of its work counted apart,
// restarted by the mark and bound only inside its press's delivery (a
// delivered mouse-down that reached no core lets no later press bind)
// while another work's and work 0's are not, finality and no press counted
// at the host's destruction, a tap dropped uninstalled completed and
// released, and two messages' taps installed in both completion orders (an
// older construction's late install after a newer one's, then the
// reverse), each work's clicks counting in its own tap alone and either
// host's destruction leaving the other's registration. That tap's final
// reading, after its host's destruction, is `control`: 1 batch of 4 ops
// with the control's 2 leaks and 1 note. Its exchange leg (the `hosted
// exchanges: ...` rows), played within the call unless |exchanges| leaves
// it apart (WidgetOpsExchanges), reads TwidgetCore's own encodings through
// Tgw::HostedAnswers(), the classifier the widget helper's stream times
// hosted renderers with: which messages expect one answer and which answer
// them, keyed by their pair and sequence; that an answer earns credit only
// within Telegram's frame cap and past checks of a constant cost, never a
// decode of a frame's list, while a frame delta, which answers the frame
// request of its sequence, earns it past TwidgetCore's own scan of its
// runs, bounded by its caps, and checks of its list length and damage,
// never a rebuild of its list; and, at a stream's end on two routes, that
// an answered route is spared while one left unanswered keeps its ordinary
// death. Its hosted deaths, finals, eviction and reading legs are
// test_widget_ops_internal.h's CheckHostedDeaths() and
// CheckHostedFinals(). On hosted taps of their own, with no work id, its
// session leg arms one FullMsgId under two session values (only the armed
// one tapped,
// separate readings, disarming one leaves the other), its stress leg
// replaces an older host and reads the newer one's `data-stress` values,
// batch counts and drops past kWidgetStressValuesLimit, and its budget leg
// gives, on each route the hosted tap has, a validated batch, a mirrored
// served-host `anim` message and a mirrored renderer `violation` one value
// past the scan budget (gaps, never parsed, each named by its route and
// bytes) and ones at it (read: one batch of one op, a host message with no
// gap, a violation `at`), then a batch with 100 KiB names: every leak
// counted, its examples and their places bounded. Without a Debug helper
// the mirrored records pass on unread and only the batch's gap counts. Its
// scan-gap leg, the same in both builds, gives onRenderOps an array cut off
// and an object in an array's place, which the tap reads, wrapped as
// WidgetHost wraps a batch, as an unparsable message and an `ops` message
// without an array: two scan gaps with their examples, no batch, both
// forwarded unchanged. Each row and its carrier:
// docs/helpers/test_widget_ops.md, "Verification". Disarms its ids before
// it returns. The caller turns each row into a Check().
[[nodiscard]] WidgetOpsSelfCheck RunWidgetOpsSelfCheck(
	WidgetOpsExchangeLeg exchanges = WidgetOpsExchangeLeg::Within);

// RunWidgetOpsSelfCheck()'s exchange leg on a thread of its own, so that a
// runner stage keeps the event loop going through the seconds its streams
// stay open: start() in the stage's run, ready() as its until and take()
// in its then, each on main, kWidgetOpsExchangesTimeout as its timeout,
// and stop() in the scenario's onFinish. The leg reads no tap and asks
// nothing of main. Its rows, named as RunWidgetOpsSelfCheck() names them,
// stay here until taken; a thread that does not start, or a leg that
// throws, gives one failed row instead. The leg stops at a deadline its
// cases share, short of that timeout, or at stop(), whichever comes
// first: from then on it starts no stream, the waits of the streams it
// has open give up within a fraction of a second, so those streams close
// and its threads return, and each case it did not finish fails its row.
// Destroying it stops the leg and joins the thread.
class WidgetOpsExchanges final {
public:
	WidgetOpsExchanges();
	WidgetOpsExchanges(const WidgetOpsExchanges &other) = delete;
	WidgetOpsExchanges &operator=(const WidgetOpsExchanges &other) = delete;
	~WidgetOpsExchanges();

	// Once, and not after stop(); a later call does nothing.
	void start();

	// Without waiting for the leg's thread: the leg stops, and none of its
	// rows is given from now on. Any time, as often as called.
	void stop();

	// Whether start() came and the rows are in.
	[[nodiscard]] bool ready() const;

	// Once ready(), the rows, the thread joined; none before that, after
	// stop() or again.
	[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> take();

private:
	struct State;

	const std::unique_ptr<State> _state;

};

// A stage's bound on WidgetOpsExchanges::ready(): the leg takes about
// 5 s, and stops at its own deadline 10 s short of this, which leaves
// room for its failing rows' bounded waits and its streams' close.
inline constexpr auto kWidgetOpsExchangesTimeout = crl::time(30000);

// Main thread, synchronous, needs Active(). |fixture| is the bytes of
// widget-sandbox's packages/conformance/probe-fixture.json: whether the
// harness is active and whether the fixture parses into its cases, each
// with string messages, a numeric htmlId and updates and an object final.
// When it parses, each case plays on a hosted tap of its own under a
// message id of peer 0, an `ops` message's array (the fixture's own bytes
// inside its envelope, which Qt must read as that array) through the
// hosted host's onRenderOps and any other message as a mirrored
// served-host record that must decode to its bytes, and
// ReadWidgetOpsLatest() is compared with the case's `expect` (htmlId,
// updates, final, awaitingTargets). Every message must be routed and
// change just what its route reads: an array one host message and one
// batch, no gap, and those fixture bytes reach the observer the tap
// wrapped, in order; a record in Debug one host message, no batch and no
// gap, consumed, and without a Debug helper it reaches that observer
// unread, the reading unchanged. Then a replacement leg with literal
// expectations: a newer selection clears the result, an older host's late
// result and theme records are not merged but its batches are scanned,
// and a later adoption without a value clears it. Without a Debug helper
// the mirrored records pass on unread. Each row:
// docs/helpers/test_widget_ops.md, "Verification". Disarms its ids before
// it returns. The caller turns each row into a Check().
[[nodiscard]] auto RunWidgetConformanceProbeCheck(const QByteArray &fixture)
-> std::vector<WidgetOpsSelfCheckRow>;

} // namespace Test
