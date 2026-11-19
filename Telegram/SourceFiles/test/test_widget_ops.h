/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "twidget/renderer.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <crl/crl_time.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The validated-ops tap of a widget message's sandbox host: what
// twidget::WidgetHost sends the product renderer, or, for a widget hosted in
// the widget helper, what the helper's validator passed and its served host
// and renderer exchanged, scanned with widget-sandbox's --attacks leak
// definition and read for the conformance and stress results, and the
// helper's own renderer work when asked. The helper's half is
// test_widget_helper_tap.h; the forwarding both halves wrap a renderer with
// is test_widget_renderer_forward.h.
// Reference: docs/helpers/test_widget_ops.md.

namespace twidget {
class Json;
class WidgetHost;
struct WidgetTheme;
} // namespace twidget

namespace Tgw {
struct PressStep;
} // namespace Tgw

namespace Test {

// The renderer factory Tgw::Runtime::construct() builds its widget host
// with, for the message |id|: its HistoryItem::globalId(), the FullMsgId
// and its session's uniqueId(), because each session allocates local
// message ids on its own and two sessions can hold different messages with
// the same FullMsgId. Returns |factory| itself, untouched, unless Active()
// and |id| is armed by ArmWidgetOpsTap(). For an armed id it returns a
// factory that calls |factory| exactly once, with the same arguments, on
// its caller's thread, and wraps the renderer that call returns in a tap
// that forwards every call and callback directly. Main thread. Without the
// harness this module defines only this function, as that identity, its
// overload below, and the seams below it: TapHostedWidget(),
// WidgetThemeObservation(), WidgetThemeSelected(), WidgetThemeScheduled(),
// WidgetThemeApplied(), WidgetHostRetired(), WidgetPressDispatched(),
// WidgetCoreDetached(), WidgetPressObserved(), WidgetLocalPressDown(),
// WidgetLocalPressReleased() and WidgetLocalPressForwarded().
[[nodiscard]] twidget::RendererFactory TapWidgetRenderer(
	GlobalMsgId id,
	twidget::RendererFactory factory);

// What Tgw::Runtime::construct() calls for a widget whose renderer runs in
// Telegram, with |work|, the RendererCore::workId() of the core it builds
// the host for (counted up from 1 and never reused, as TapHostedWidget()
// gets it): the same selection and wrapping as the overload above, and for
// an armed id it registers |work| (unless 0) with the generation that
// selection made, for WidgetPressDispatched(), WidgetLocalPressDown(),
// WidgetLocalPressReleased(), WidgetLocalPressForwarded() and
// WidgetCoreDetached(), while the returned factory or the wrapper it built
// lives (the overload above registers none); the wrapper also reports to
// the tap, with |work|, each renderer message it forwards to the host.
// Main thread. Without the harness this module defines this function too,
// returning |factory| itself.
[[nodiscard]] twidget::RendererFactory TapWidgetRenderer(
	GlobalMsgId id,
	uint64 work,
	twidget::RendererFactory factory);

// The hosted counterpart of TapWidgetRenderer(), for a host whose engine,
// validator and renderer run in the widget helper, which has no renderer
// factory to wrap: Tgw::Runtime::construct() calls it for the message |id|
// on the main thread, once per hosted construction, with |work|, the
// RendererCore::workId() of the core it builds the host for (counted up
// from 1 and never reused, so a later core never shares it; 0 for none),
// and |theme|, the theme the host's Job carries into its constructor.
// Returns an empty function unless Active() and |id| is armed by
// ArmWidgetOpsTap(). For an armed id it selects the tap's next generation
// at once, as a wrapping factory's selection does, and returns a function
// for Build()'s worker to call once with the hosted WidgetHost, right after
// its constructor returned and the runtime set its observers, before the
// host is bound or started. That call records |theme| as the theme the
// runtime applied to the generation (WidgetOpsLatest's `applied...`
// fields, its first theme serial), registers the host for
// WidgetThemeObservation(), WidgetThemeSelected(), WidgetThemeScheduled(),
// WidgetThemeApplied() and WidgetHostRetired() and |work| for
// WidgetPressDispatched() and WidgetPressObserved(), and
// wraps the host's onRenderOps (the validated batches, which only an
// observer set before start() gets), onPlatformMessage and onDeath, and
// onChange (to see the host's own rendered acknowledgment), each calling
// the observer it wrapped, if any, after it recorded: onPlatformMessage
// consumes a well-formed Mirrored record of the helper's tap once it asked
// for them and passes every other message on unchanged. What the widget
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
// Without the harness this module defines this function too, returning an
// empty function.
[[nodiscard]] auto TapHostedWidget(
	GlobalMsgId id,
	uint64 work,
	const twidget::WidgetTheme &theme)
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
// Tgw::HostedInput::press() and WidgetPressObserved()'s Sent, or locally
// WidgetLocalPressDown()). When |work| is the work of the marked
// generation's host with an installed hosted tap, or the local work
// TapWidgetRenderer() registered for it, and that tap delivers its mark's
// press (DeliverMarkedPress()), the first such press inside that delivery
// binds the mark to that work and serial: the press the fixture played. A
// press dispatched before or after the delivery binds nothing, one the work
// still had queued or a later one of the same work included. Without the
// harness this module defines this function too, doing nothing.
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

// Tgw::RendererWork::pointerPress() calls this on the widget render thread
// for a widget whose renderer runs in Telegram, with |work|, its id (the
// RendererCore::workId() TapWidgetRenderer() registered), and |serial|, the
// pointer serial RendererCore::pointerPress() dispatched the press with
// (the one WidgetPressDispatched() got), when the work accepted that
// primary mouse down and gave it to its renderer. A press the work did not
// accept, and a secondary or finger press, call nothing. When |work| is
// registered, the down is the marked interaction's
// (`downsConfirmed`) when |serial| is the played press's while its
// generation is the newest, and either way the press the work's next
// WidgetLocalPressReleased() ends; any other down counts in
// `pressStepsElsewhere`. Without the harness this module defines this
// function too, doing nothing.
void WidgetLocalPressDown(uint64 work, uint64 serial);

// Tgw::RendererWork::releasePointer() calls this on the widget render
// thread for a widget whose renderer runs in Telegram, with |work|, its id,
// when the primary mouse press its renderer took (WidgetLocalPressDown())
// ends with an up, or with a cancel when the chat took the press, and
// |out|, what the renderer answered that release with, before the work
// forwards it: a trusted `click` event among them is the click that press
// made, and none means it made none. A press dropped without a release (the
// work detached, stopped or went off screen) calls nothing; the next press
// of the work starts over. When |work| is registered and the press this
// release ends is the played one, its down taken since the mark and its
// generation still the newest, the release counts in `upsConfirmed`, and
// its click counts in `clicksCompleted` only when it reaches the host: the
// first trusted click that the work's forwarding of |out| hands the wrapper
// TapWidgetRenderer() built, on this thread, before the release's
// WidgetLocalPressForwarded(), is the click of the fixture's own press, as
// the host got it. Every other release and its click count in
// `pressStepsElsewhere`. Without the harness this module defines this
// function too, doing nothing.
void WidgetLocalPressReleased(
	uint64 work,
	const std::vector<twidget::Json> &out);

// Tgw::RendererWork::releasePointer() calls this on the widget render
// thread for a widget whose renderer runs in Telegram, with |work|, its id,
// right after it forwarded what its renderer answered a release: what that
// release's WidgetLocalPressReleased() got went out between the two calls,
// on this thread, through the host link to the live host's renderer
// callbacks (the tap's wrapper sees it there), or nowhere once
// RendererCore::detach() closed that link. Every mouse release the work's
// renderer took calls it, a secondary one (which reports no
// WidgetLocalPressReleased()) included. It ends that release's forwarding:
// a click of the played press that did not reach the wrapper by then (the
// link closed, the work not ready) never completes the mark. Without the
// harness this module defines this function too, doing nothing.
void WidgetLocalPressForwarded(uint64 work);

// Tgw::RendererCore::detach() calls this on the main thread, with |work|,
// its RendererCore::workId(), first, before it closes the core's host link
// and posts the work's detach (Runtime::retire() and ~Runtime() detach a
// live core before dropping, keeping or releasing it and before they
// retire its host): from the close on nothing the work's renderer answers
// reaches the host, while a step the work already took can still run on
// the widget render thread and report to WidgetLocalPressReleased() or
// WidgetPressObserved(), and a wrapper or host observer can still hold the
// registration. Called for every core. When |work| is the work of a host
// with an installed hosted tap, or the local work TapWidgetRenderer()
// registered, and the mark is bound to it, the tap revokes the mark at
// once, under its lock, unless the played press's click already completed
// (`markDetached`): a step of that work reported later counts in
// `pressStepsElsewhere` alone and no click of it completes the mark, also
// when it comes through a lookup that held the registration before this
// call. A click completed before it stays; another work's detach changes
// nothing. Without the harness this module defines this function too,
// doing nothing.
void WidgetCoreDetached(uint64 work);

// Everything below is harness-only: declared here and deliberately left
// undefined in a build without the harness, because no production code
// reads it.

// One armed message's record, shared by the main thread, the host's Build
// worker, its host thread and its deleter worker. Opaque; read it with
// ReadWidgetOps().
class WidgetOpsTap;

// A snapshot of one tap. Every count covers every renderer built for the
// message while it was armed and is never reset, except the trusted event
// fields and the press readings (`markedGeneration` to `markDetached`),
// which count from the last MarkWidgetOpsEvents().
// Counts are uncapped; every example list keeps
// only its first 64 entries.
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

	// Wrapping factories handed out by TapWidgetRenderer() and hosted taps
	// handed out by TapHostedWidget(), on main.
	int factoriesSelected = 0;
	// Product factory calls that returned or threw; the exception of one
	// that threw is rethrown to WidgetHost after it is counted. A hosted tap
	// completes when it is installed on its host or released uninstalled,
	// and never throws.
	int factoriesCompleted = 0;
	int factoriesThrew = 0;
	// Wrapping factories and hosted taps whose last copy is gone, called or
	// not: none of them can build a renderer or reach a host any more.
	int factoriesReleased = 0;
	// Wrappers built, and destroyed only after their product renderer. A
	// hosted host's observers count as built when the tap installs them and
	// as destroyed with the host, whether or not its renderer was kept.
	int wrappersBuilt = 0;
	int wrappersDestroyed = 0;

	// Host -> renderer messages, every type; of a hosted host its validated
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
	// The marked interaction, of a hosted host or a local one: the
	// generation the last MarkWidgetOpsEvents() found newest; the work and
	// pointer serial of the first primary press the chat dispatched to a
	// tapped or registered work of that generation while
	// DeliverMarkedPress() delivered the fixture's own mouse-down
	// (WidgetPressDispatched(); 0 when none did, for good: the binding is
	// sealed when that delivery returns), the press the fixture played; and,
	// hosted only, the number that press got when its work sent it
	// (WidgetPressObserved()'s Sent with that serial; 0 before it, and
	// always 0 locally). Hosted, its down and up answered on a route still
	// active (delivery only) and the renderer's click decision for that up
	// reported after it; locally, its down and its release taken by the
	// work's renderer (WidgetLocalPressDown(), WidgetLocalPressReleased())
	// and the trusted click that release answered, counted when the work's
	// forwarding of that answer hands it to the wrapper, before the
	// release's WidgetLocalPressForwarded(): the click the host got. Each
	// counted for that one press alone while the marked generation is the
	// newest and its core not detached (hosted: its host not retired) before
	// the click (`markDetached`), and `clicksCompleted` only after its up. A
	// step of any other press, an earlier one of the same work still queued
	// at the mark, a later one of the same work after an undelivered mark,
	// the release of a later down of the same work or one of an older
	// generation that completes late included, and every step after a
	// newer generation was selected, the marked work's core was detached or
	// its hosted host retired, counts in `pressStepsElsewhere` alone. A
	// down or up, even both answered, never counts as a click.
	// `clicksCompleted` is the click reading of a hosted host whose helper
	// has no tap (WidgetHelperMirrors() false), where `trustedClicks` stays
	// 0; with the Debug tap, and locally, `trustedClicks` counts the
	// generation's trusted clicks whatever press made them, and only
	// `clicksCompleted` is the played press's own.
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
	// Whether the core of the work the mark is bound to, hosted or local,
	// was detached (WidgetCoreDetached(), before its host link closed), or
	// the hosted host of that work was retired (WidgetHostRetired()), before
	// the played press's click completed: the mark's later steps and its
	// click no longer count, while what it counted before stays.
	bool markDetached = false;

	// The host's post-load ack (`query` with numeric `qid` -1), the
	// renderer's answer to it (`result` with numeric `qid` -1) and the
	// host's death notice (`frozen` with `fatal` true; of a hosted host
	// every onDeath, whatever its reason's text, never the served host's
	// mirrored `frozen`: WidgetHost never reports the end its owner asked
	// for).
	int ackQueries = 0;
	int ackResults = 0;
	int fatalFrozen = 0;
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
	// The raw `{"type":"theme",...}` host message last sent to its renderer.
	// Of a hosted host the served host's, as the Debug helper's tap mirrored
	// it; without that tap (a Release harness) the widget's own reading
	// alone: `{"type":"theme","theme":{"colorScheme":...,"params":...}}` of
	// the newest effective reading, empty before the first.
	std::string themeMessage;
	// The scheme its renderer was given: the factory's theme argument, then
	// each setTheme(). Empty until its wrapper is built; of a hosted host
	// until the Debug helper's tap reported the scheme its renderer was built
	// with, and without that tap (a Release harness) `effectiveScheme` of
	// the newest effective reading, never the theme the runtime asked for:
	// the first reading sets it, each later reading that changes it counts
	// in `schemeChanges`.
	std::string scheme;
	// Of a hosted host, the theme the runtime applied to it: the one its
	// constructor got (TapHostedWidget()), then each WidgetThemeApplied(),
	// recorded once the application returned. The scheme as the host keeps
	// it ("dark", else "light"), and the `{"type":"theme",...}` message that
	// theme gives the helper's renderer, built with the SDK's normalization
	// as WidgetHost::themeMessage() builds it (the checked palette under its
	// 15 keys, the button and text tokens): the expectation, never a
	// reading of the widget. Empty for a local host.
	std::string appliedScheme;
	std::string appliedThemeMessage;
	// Of a hosted host, what the widget itself read after the runtime
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
	// The 1-based count of wrapping factories selected for the message when
	// this host's was: above 1, more than one host was constructed for it,
	// and these fields, like a runtime's log, cover only the last.
	int generation = 0;
	// `data-conformance` values this host published, the probe fixture's
	// `updates`.
	int updates = 0;
	// Advances whenever `conformance` changes, a clear included; never reset.
	// A `data-stress` value never moves it.
	int serial = 0;
	// setTheme() calls of this host that changed `scheme`.
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
	// attribute or a theme, setTheme() calls and builds of an older host's
	// wrapper after a newer host was selected: ignored above, counted here;
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

// Main thread. Later constructions for |id| get the product factory itself;
// factories already handed out and the wrappers they built keep the tap.
// Pass the id that was armed, as kept by the caller, not one rebuilt later
// from whichever session is active then; no other id is touched.
void DisarmWidgetOpsTap(GlobalMsgId id);

// Any thread. No factory call and no wrapper of the tap is still pending
// once factoriesReleased == factoriesSelected and wrappersDestroyed ==
// wrappersBuilt; only a reading taken after that is final.
[[nodiscard]] WidgetOpsReading ReadWidgetOps(const WidgetOpsTap &tap);

// Any thread. A snapshot taken under the tap's lock, `stress` copied whole;
// a caller that parses `conformance` can cache the result by `generation`
// and `serial`.
[[nodiscard]] WidgetOpsLatest ReadWidgetOpsLatest(const WidgetOpsTap &tap);

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

// Its local twin, of a host whose renderer Telegram builds: the scheme that
// renderer was given (`scheme`), Unobserved while it has none, Wrong when
// it is not |scheme|; with |before|, only a scheme given since (the same
// generation, more `schemeChanges`).
[[nodiscard]] WidgetThemeCheck CheckGivenTheme(
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
// work of the marked generation, hosted or local
// (WidgetPressDispatched()), binds the mark to its work and pointer
// serial, whose later steps go on counting as the marked
// interaction's. A mark still unbound at the seal stays unbound
// until the next mark, so no press dispatched later, of the same work
// included, can complete it. A second call, or one after the mark was
// superseded, only calls |deliver|.
void DeliverMarkedPress(WidgetOpsTap &tap, Fn<void()> deliver);

// Whether this build's widget helper has the Debug tap that mirrors a
// hosted host's renderer messages, scheme and theme message
// (test_widget_helper_tap.h): true in Debug. False in a Release build with
// the harness, whose hosted readings of the theme are the widget's
// effective ones and whose hosted click reading is `clicksCompleted`.
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
	// The control tap's final reading, after its factory and wrapper are gone.
	WidgetOpsReading control;
};

// Main thread, synchronous, needs Active(). Drives TapWidgetRenderer()
// with a stub product renderer under message ids of peer 0, which no real
// message has in any session: unarmed identity, armed wrapping and
// forwarding with exact bytes, order and threads, the scanner's 2-leak,
// 1-note control followed by a removal batch, scan gaps, the ack, frozen,
// violation and trusted-event readings, two `limit` reports counted twice
// with their examples and delivered in order, a throwing product factory,
// normal destruction, and one FullMsgId in two sessions (only the armed one
// wrapped, separate readings when both are armed, disarming one leaves the
// other). Its stress leg replaces an older host whose wrapper adopted <html>
// with a value, then gives the newer one a one-op adopting batch with a
// `data-stress` value and batches of 3 and 5 ops with an `attr` value
// each: 3 batches, 9 ops, a largest of 5 and the three values in order,
// receipts non-decreasing, conformance untouched; the older wrapper's late
// 6-op batch with a value is scanned but counts in none of them; a batch
// without a value counts; values past kWidgetStressValuesLimit only count
// as dropped; a later adoption clears values and drops, not counts. Its
// hosted leg drives TapHostedWidget() on a hosted WidgetHost built without
// a channel and never started, calling the observers the tap installed
// directly: no tap unarmed, selection at once, one install, release by the
// last copy, a validated batch scanned and passed on, each mirrored way
// recorded (in Debug; without a Debug helper every message passes on), a
// malformed mirror passed on, a served frozen no death, every onDeath one
// (one reading terminated too), the host's rendered acknowledgment counted
// once and its one theme probe (a widget with no runtime: no reading, the
// theme unobserved), helper work records kept within their room, dropped
// past it, malformed ones (unordered, over an hour, negative, an
// overwaited interval) counted, full 32-bit counts kept and a stamp that
// maps past the clock's range refused, the construction's and a later
// applied theme recorded as the expectation (never as the renderer's
// scheme) while another host's theme is not, the effective readings (a
// superseded, an oversized and a malformed one not kept, a mismatching one
// kept with its difference, a matching one, and without a Debug helper the
// scheme, its changes and the theme message they give), a theme selected
// while another theme's job runs pending from its selection until its own
// job applied and read it, downs and completed clicks of its work counted
// apart, restarted by the mark and bound only inside its press's delivery
// (a delivered mouse-down that reached no core lets no later press bind)
// while another work's and work 0's are not, finality and no press counted
// at the host's destruction, a tap dropped uninstalled completed and
// released, and two messages' taps installed in both completion orders (an
// older construction's late install after a newer one's, then the
// reverse), each work's clicks counting in its own tap alone and either
// host's destruction leaving the other's registration. Its local press leg
// registers two local generations' works through TapWidgetRenderer() and
// plays the same marks through WidgetPressDispatched(),
// WidgetLocalPressDown(), WidgetLocalPressReleased() and
// WidgetLocalPressForwarded(): an older generation's release after the
// mark, a press queued before it, a mouse-down that reached no core
// followed by a genuine click, a down with no release, a later down before
// the release and a selection between the down and the release count no
// click, the played stable click counts one, its trusted click reaching the
// tap through the wrapper inside its release's forwarding, and the
// registration lives with the wrapper after the factory is gone and ends
// with both. Its local retirement leg detaches a registered work
// (WidgetCoreDetached()) while its wrapper stays: the played press's up
// queued across the detach, through the seam or a lookup made before it,
// beside an earlier trusted click of another press, a release taken before
// the detach whose click reached no host and a click handed the wrapper
// after it count no click and revoke the mark, while a click that reached
// the host before the detach stays and another work's detach changes
// nothing. Its budget leg gives
// a wrapper a host message and a renderer message one value past the scan
// budget (each a gap, never parsed) and ones at it (scanned), then a batch
// with a 100 KiB attribute name and 100 more matches than examples kept:
// every match counted, at most 64 examples within 64 KiB.
// Disarms its ids before it returns. The caller turns each row into a
// Check().
[[nodiscard]] WidgetOpsSelfCheck RunWidgetOpsSelfCheck();

// Main thread, synchronous, needs Active(). |fixture| is the bytes of
// widget-sandbox's packages/conformance/probe-fixture.json, which gives each
// case's expected values. Feeds each case's messages through a wrapper that
// TapWidgetRenderer() builds around a stub product renderer under message
// ids of peer 0 and compares ReadWidgetOpsLatest() with the case's `expect`:
// htmlId, updates, final and awaitingTargets. Two legs with literal
// expectations follow: a replacement host (a newer selection clears the
// result; a retiring wrapper's late result, theme and setTheme() are
// ignored but its batches scanned; a later adoption without a value clears
// it) and the factory theme, setTheme() and theme message. Disarms its ids
// before it returns. The caller turns each row into a Check().
[[nodiscard]] auto RunWidgetConformanceProbeCheck(const QByteArray &fixture)
-> std::vector<WidgetOpsSelfCheckRow>;

} // namespace Test
