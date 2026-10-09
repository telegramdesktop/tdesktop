/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/notify/data_peer_notify_settings.h"

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace MTP::details {
class SerializedRequest;
} // namespace MTP::details

namespace Test {

class Runner;

// A local, restored override of one peer's notify settings.
//
// Why it exists. The golden test account's default user settings are
// served as mute_until=2147483647, so the service notifications chat
// (777000) and every private chat without a mute value of its own read
// muted: Data::NotifySettings::isMuted falls through to the defaults,
// Window::Notifications::System::computeSkipState answers Skip,
// System::schedule returns at skipNotification and
// NativeManager::doShowNotification never runs. A campaign that must see
// a notification delivered meets a skipped one. Observed in Run 2 of
// 2026/10/04/show-hidden-sender-gram-transfers-in-the-service-chat.
//
// The hazard. The obvious product calls that unmute a chat -
// Data::NotifySettings::update, resetToDefault, defaultUpdate and
// clearExceptions - all reach ApiWrap::updateNotifySettingsDelayed, which
// sends account.updateNotifySettings after kNotifySettingSaveTimeout
// (1 s, apiwrap.cpp) and changes the SHARED account's served settings for
// every later run on every checkout. Nothing in this module calls any of
// them, and a campaign must not either.
//
// The one call used is Data::NotifySettings::apply(const MTPNotifyPeer &,
// const MTPPeerNotifySettings &), exactly what Api::Updates::feedUpdate
// does for mtpc_updateNotifySettings. It goes through
// PeerNotifySettings::change and, only when that reports a change,
// updateException (exception membership follows the peer's own
// mute_until; self, inaccessible users, forbidden chats and channels and
// communities never join), updateLocal (History::setMuted when the cached
// flag flips, which fires peerUpdated(Notifications), otherwise that
// update directly; a muted result calls clearIncomingFromHistory, which
// drops the history's pending notifications; cacheSound, which can issue
// read-only ringtone document requests for a ringtone id and nothing
// else) and Core::App().notifications().checkDelayed(). It forms no
// request.
//
// What an override lives through. Notify settings are not persisted
// locally (storage/ never reads notify()), so an override dies with the
// process and a relaunch reads the served state again. A served writer
// inside the window replaces it: an updateNotifySettings push,
// History::applyDialog on a dialogs refresh, ApplyUserUpdate from
// users.getFullUser, the chat and channel full readers, and the answers of
// ApiWrap::requestNotifySettings. The restore reports that as intact=0
// and restores the capture anyway - it reports, it never fights.
//
// Not covered: forum topics (their own PeerNotifySettings) and the
// community fallback (a community channel's mute read by its members).
// The override is one peer's own settings.
//
// Value presence. PeerNotifySettings keeps "known" and an optional value
// object of five optional fields. Public API reads "known" through
// settingsUnknown() and the fields through the getters and serialize(),
// but cannot tell a value object with every field unset from no value
// object at all - serialize() answers flags 0 for both - while change()
// treats the two differently, so the capture reads the value object's
// presence through an explicit-instantiation accessor confined to
// test_notify_override.cpp. No production file gains an accessor, a
// friend or a seam.
//
// A peer whose settings are still unknown is refused by name: an apply
// would make them known, and no apply can make them unknown again.
//
// The locality reading (no account.updateNotifySettings formed while an
// override is live) needs a one-statement hook the disposable overlay
// pastes first in MTP::details::Session::sendPrepared; the request
// observer this module declares for it, InterceptNotifyRequest, quotes
// the exact paste. Without the hook that leg is N/A by name.

// Every field Data::PeerNotifySettings keeps, with value presence, plus
// the product's derived readings of the peer. ios_sound, android_sound,
// stories_hide_sender and the stories sounds are not stored by the
// client, so no apply can change them and they are not captured.
// |read| is false only on a default-constructed state. |historyMuted| is
// History::muted() (the cached Data::Thread flag) or -1 without a loaded
// history; |exception| is membership in the peer's default type's
// exceptions list, or -1 when not read. A |sound| is read through
// sound(), never through serialize(), whose SerializeSound maps a local
// sound with an empty title to the default one.
struct PeerNotifyState {
	bool read = false;
	bool known = false;
	bool valuePresent = false;
	std::optional<TimeId> muteUntil;
	std::optional<Data::NotifySound> sound;
	std::optional<bool> silentPosts;
	std::optional<bool> showPreviews;
	std::optional<bool> storiesMuted;
	bool muteUnknown = false;
	bool muted = false;
	int historyMuted = -1;
	int exception = -1;

	friend bool operator==(
		const PeerNotifyState &a,
		const PeerNotifyState &b) = default;
};

// Pure reads of the running session: never NotifySettings::request, never
// a history or peer created. |muted| is NotifySettings::isMuted(peer) as
// the product reads it, |muteUnknown| beside it.
[[nodiscard]] PeerNotifyState ReadPeerNotifyState(not_null<PeerData*> peer);

// "known=%1 value=%2 mute_until=<n|unset>
// other_sound=<unset|none|default|ringtone:<id>|local:[title][data]>
// silent=<0|1|unset> show_previews=<0|1|unset> stories_muted=<0|1|unset>
// muteUnknown=%1 muted=%2 historyMuted=<-1|0|1> exception=<-1|0|1>",
// or "unread" for a state that was never read.
[[nodiscard]] QString PeerNotifyStateText(const PeerNotifyState &state);

// The stored fields of the three default settings, each with muted=<0|1>
// of NotifySettings::isMuted(DefaultNotify):
// "user{...} group{...} broadcast{...}". The exceptions lists are
// deliberately left out: an override legitimately adds its peer to them,
// and that is read in the peer's own state.
[[nodiscard]] QString DefaultNotifyStateText(
	not_null<Main::Session*> session);

// The caller's override. An unset member keeps the captured value, so
// the override changes exactly the fields named here.
struct NotifyOverrideValues {
	std::optional<TimeId> muteUntil;
	std::optional<Data::NotifySound> sound;
	std::optional<bool> silentPosts;
	std::optional<bool> showPreviews;
	std::optional<bool> storiesMuted;
};

// mute_until=0 and other_sound=none: unmuted, and computeSkipState marks
// the notification silent, so an unattended run plays no sound and
// bounces no dock icon. Saved Messages ignores its own sound and follows
// the default one (NotifySettings::sound).
[[nodiscard]] NotifyOverrideValues UnmutedSilentNotify();

// mute_until=INT_MAX (the product's "forever") and other_sound=none.
[[nodiscard]] NotifyOverrideValues MutedSilentNotify();

// The named refusals. A refused override applies nothing and still
// returns a handle, so the caller decides between a fixture gate
// (Stage::skipReason) and a FAIL.
// - SettingsUnknown: peer->notify().settingsUnknown(). An apply would
//   make the settings known, and no apply can make them unknown again,
//   so no restore could put the capture back.
// - AlreadyOverridden: an applied, not yet restored override of the same
//   peer in the same live session exists. A nested capture would capture
//   the outer override, and its restore would put the override back.
enum class NotifyOverrideGate {
	None,
	SettingsUnknown,
	AlreadyOverridden,
};

// "none", "settings-unknown", "already-overridden".
[[nodiscard]] QString NotifyOverrideGateName(NotifyOverrideGate gate);

// One NotifyOverride::restore() reading. |applied| is true only on the
// call that applied the capture back; |intact| is read on that call
// only, before the apply: whether the peer still read exactly the state
// the override left, so false names a served writer that replaced the
// override inside the window. |equal| compares the re-read whole state -
// stored fields with value presence and the derived readings - with the
// capture. restored() is also true for a refused handle (nothing was
// applied, so nothing is left to restore) and for a gone session (notify
// settings are not persisted, so an override cannot outlive its session).
struct NotifyRestoreReading {
	NotifyOverrideGate refused = NotifyOverrideGate::None;
	bool applied = false;
	bool intact = false;
	bool sessionGone = false;
	bool equal = false;
	QString captured;
	QString now;

	[[nodiscard]] bool restored() const;
};

// "applied=<0|1> intact=<0|1|-> equal=<0|1> captured{...} now{...}", with
// PeerNotifyStateText inside the braces and intact=- when this call
// applied nothing; "refused=<gate>: nothing was applied, nothing to
// restore" for a refused handle; "session=gone: the override ended with
// the session" for a gone session.
[[nodiscard]] QString NotifyRestoreText(const NotifyRestoreReading &reading);

// One Test::Check(reading.restored(), what, NotifyRestoreText(reading)),
// whose details are printed on both verdicts. Returns that verdict.
bool CheckNotifyRestored(
	const NotifyRestoreReading &reading,
	const QString &what);

// The live override of one peer, always held through the shared_ptr
// OverrideNotify returns, which is never null. It holds the session only
// weakly and the peer only by id, so it never keeps either alive and
// never dangles: every reading resolves the peer from the live session
// again. The module keeps every handle until the process ends, which is
// what its Runner::onFinish restore walks.
class NotifyOverride final {
public:
	// True when the override was applied, false for a refused handle.
	[[nodiscard]] bool applied() const;
	[[nodiscard]] NotifyOverrideGate gate() const;
	[[nodiscard]] PeerId peerId() const;

	// Null once the session is gone.
	[[nodiscard]] Main::Session *session() const;

	// The whole state read before anything was applied, refused handles
	// included, and the whole state read right after the apply.
	[[nodiscard]] const PeerNotifyState &captured() const;
	[[nodiscard]] const PeerNotifyState &overridden() const;

	// The end of the module's request record taken immediately before the
	// apply: AppendNotifyLocalityCheck walks the requests formed from here.
	[[nodiscard]] int firstRequestMark() const;
	[[nodiscard]] crl::time appliedAt() const;

	// When restore() was last called (every call, including the verifying
	// ones), 0 before the first; the locality tail is measured from it.
	[[nodiscard]] crl::time lastRestoreAt() const;

	// True once a restore() applied the capture back.
	[[nodiscard]] bool restored() const;

	// The first call on an applied handle reads |intact|, applies the
	// capture back through the same local call and re-reads; every later
	// call applies nothing and only re-reads and compares, so a campaign
	// calls it from its window's end, from teardown and from
	// Runner::onFinish alike. The comparison, never the call, is the
	// verdict: pass the reading to CheckNotifyRestored.
	[[nodiscard]] NotifyRestoreReading restore();

private:
	NotifyOverride() = default;

	base::weak_ptr<Main::Session> _session;
	PeerId _peerId = 0;
	NotifyOverrideGate _gate = NotifyOverrideGate::None;
	PeerNotifyState _captured;
	PeerNotifyState _overridden;
	int _requestMark = 0;
	crl::time _appliedAt = 0;
	crl::time _lastRestoreAt = 0;
	bool _applied = false;
	bool _restored = false;

	friend std::shared_ptr<NotifyOverride> OverrideNotify(
		not_null<Runner*> runner,
		not_null<PeerData*> peer,
		const NotifyOverrideValues &values);

};

// Captures |peer|'s whole state, then - unless a gate refuses - applies
// the capture with |values| over it through the one local call the
// updateNotifySettings handler makes. The applied shape always carries a
// value object (value=1); a member of |values| left unset keeps the
// captured field, including its presence. Pick the peer and call this in
// one main-thread turn when the peer was chosen for its settings being
// known or unknown, so no account.getNotifySettings answer lands in
// between. Writes one Note naming the peer, the gate or the request mark,
// and the readings. Never null.
//
// The module registers one Runner::onFinish callback of its own, on the
// first call of the process, which walks every handle in REVERSE order and
// writes CheckNotifyRestored(handle->restore(), "notify override:
// onFinish: notify settings of peer <id> restored to the captured values")
// for each applied one: a restore for a handle the campaign left live, a
// verification for one it already restored. Runner::finish() runs its
// callbacks FIFO, so one LIFO walk is what unwinds nested overrides of
// different peers in the right order.
[[nodiscard]] std::shared_ptr<NotifyOverride> OverrideNotify(
	not_null<Runner*> runner,
	not_null<PeerData*> peer,
	const NotifyOverrideValues &values = UnmutedSilentNotify());

// The request observer the locality reading needs. Paste this, with
// #include "test/test_notify_override.h" // OVERLAY. under #ifdef _DEBUG,
// as the FIRST statement of MTP::details::Session::sendPrepared
// (mtproto/session.cpp) - before any other suppressing hook, such as a
// wallet intercept's, so it sees every request:
//
// #ifdef _DEBUG
// 	if (Test::InterceptNotifyRequest(request)) { // OVERLAY.
// 		return;
// 	}
// #endif // _DEBUG
//
// Why sendPrepared. It is the single main-thread funnel into a session's
// send map: Sender::send -> Instance::send -> Instance::Private::sendRequest
// reach it synchronously for every first send, with the very requestId
// send() returns, and every Instance::Private resend reaches it again,
// while SessionPrivate's own resends re-queue only requests that were
// already sent - which a suppressed one never is. It sees request
// FORMATION, before any network: a request recorded here exists whether
// or not it ever leaves the app.
//
// It reads the body constructor in place at
// SerializedRequest::kMessageBodyPosition (the method itself: the
// invokeWithLayer and container wrappers are added later, at transmission)
// and appends { requestId, constructor, time, suppressed } to a module
// record that grows to the process end; it writes nothing to the test log.
//
// The fuse. Under Test::Active() it returns true - suppress, the paste
// returns before the send map - for every account.updateNotifySettings,
// so a campaign that pastes it cannot change the shared account's served
// notify settings, whatever it calls. The suppressed request stays
// registered and unanswered, which is harmless: ApiWrap sends that request
// with no handlers. Every other request returns false and is sent as
// usual.
//
// Main thread only; it asserts that and locks nothing. The hook is the
// overlay's hunk, never retained, and this module adds no production seam.
// Like everything this header declares, it is defined only in a Debug
// build, and deliberately left undefined in Release: its only caller is
// that overlay hunk under _DEBUG.
[[nodiscard]] bool InterceptNotifyRequest(
	const MTP::details::SerializedRequest &request);

// The locality reading: one stage, "<what>: no account.updateNotifySettings
// from the first override until more than 1 s after the last restore".
// |resolve| is called on every reading and may return null.
//
// - N/A "no applied override" when |resolve| answers null or a refused
//   handle; N/A "observer hook not installed" when no request at all has
//   reached InterceptNotifyRequest since launch (a ready session has
//   already sent its startup requests through sendPrepared, so an empty
//   record means the paste is missing).
// - Waits until more than 2 s have passed since the handle's last
//   restore() - longer than kNotifySettingSaveTimeout (1 s, apiwrap.cpp)
//   plus sendNotifySettingsUpdates' 5 ms delay and timer slack, so a
//   request an override's apply had queued through
//   ApiWrap::updateNotifySettingsDelayed would already be formed.
// - Then FAILs by name when the handle was never restored or its session
//   is gone; otherwise sends the deliberate read-only control, a raw
//   account.getNotifySettings(inputNotifyUsers) with no handlers (its
//   answer is dropped and nothing is applied; never
//   ApiWrap::requestNotifySettings, which applies the answer), which
//   reaches the hook synchronously inside send(). A Test::DiscriminatingScan
//   walks the requests formed from the handle's firstRequestMark(): a
//   subject is any account.updateNotifySettings, the control is the record
//   carrying the control's own request id AND constructor. Other
//   account.getNotifySettings records are product reads, counted and named
//   in the details, matching neither role.
// - One Test::Check: the scan decided, no subject, and the control formed
//   more than 1 s after the last restore; the details carry every mark,
//   time and the subject list.
//
// Sends exactly one request, read-only, and never a request-forming one.
void AppendNotifyLocalityCheck(
	not_null<Runner*> runner,
	Fn<std::shared_ptr<NotifyOverride>()> resolve,
	const QString &what);

// The helper measuring itself on the running session: its own
// waitForSessionReady(), five stages and the shared locality stage. It has
// no chats wait of its own (startup waits are opt-in): the first stage
// waits for exactly the readings it needs.
//
// 1. Resolve the fixture peers. The subject is the service notifications
//    chat (777000) once it is loaded with known settings, a decided mute
//    and a loaded history - on the golden account it reads muted through
//    the default user mute, the case this module exists for - or Saved
//    Messages once the chats list is loaded while 777000 is still not
//    ready. The other peer is the remaining candidate when it is loaded.
//    The other peer's reading and the three defaults' are recorded as the
//    "before" every later stage compares with. Neither candidate ready
//    within kStartupStageTimeout is a named fixture gate: every later stage
//    is N/A with both readings.
// 2. Override the subject, inverting its served isMuted
//    (UnmutedSilentNotify for a muted subject, MutedSilentNotify
//    otherwise). Checks: applied with gate none; isMuted(peer),
//    isMuted(history) and the history's Data::Thread::muted() all report
//    the inverse; the stored fields are the requested mute and sound with
//    every other field, presence included, as captured; overridden() !=
//    captured(), the discriminating counterpart of the restore's equality;
//    the other peer and the defaults unchanged.
// 3. Refusals inside the window. A second override of the live subject is
//    refused already-overridden and leaves it exactly overridden(). Then,
//    in the same main-thread turn as the pick, so no
//    account.getNotifySettings answer can make it known in between, one
//    peer the session already holds with unknown settings - loaded ones
//    first, because apply(PeerId) would really change a loaded one, so
//    only its refusal shows the gate doing the work; then the lowest id -
//    is refused settings-unknown with its whole state, known=0 value=0,
//    unchanged. Nothing is created or requested to find it; no such peer
//    is a named fixture gate on that one row.
// 4. Restore at the window's end: CheckNotifyRestored on the whole state,
//    applied=1 intact=1, the three readers back to the served value, the
//    other peer and the defaults unchanged.
// 5. Teardown: a second restore applies nothing and still reads equal.
//    It is the last restore call, so the locality tail runs from it.
// 6. AppendNotifyLocalityCheck as "notify override self-test: locality".
//
// The locality leg needs the InterceptNotifyRequest paste in the overlay
// and is N/A by name ("observer hook not installed") without it. The
// module's own Runner::onFinish verifies the handle once more (applied=0,
// equal); the self-test relies on that row instead of registering a finish
// callback of its own.
//
// It sends exactly one request itself, the read-only control, and calls no
// request-forming path; the product's own apply can at most ask for the
// ringtone list when a captured sound is a ringtone id. It leaves no
// override behind, needs no wallet and no fixture secret, and emits no
// deliberate FAIL: every row is expected to PASS on the golden account
// with the hook pasted.
void AppendNotifyOverrideSelfTest(not_null<Runner*> runner);

} // namespace Test
