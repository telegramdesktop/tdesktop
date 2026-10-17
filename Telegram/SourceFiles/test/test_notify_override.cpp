/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_notify_override.h"

#ifdef _DEBUG

#include "apiwrap.h"
#include "core/application.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

namespace Test {
namespace {

// apiwrap.cpp's kNotifySettingSaveTimeout, mirrored because it lives in
// an anonymous namespace there: updateNotifySettingsDelayed arms
// _updateNotifyTimer with it, and sendNotifySettingsUpdates then forms
// account.updateNotifySettings.
constexpr auto kNotifySaveTimeout = crl::time(1000);

// More than the save timeout plus sendNotifySettingsUpdates' kSmallDelayMs
// (5 ms) plus timer slack: a request an apply had queued is formed by then.
constexpr auto kNotifyLocalityTail = crl::time(2000);

// One request handed to MTP::details::Session::sendPrepared.
struct NotifyRequestRecord {
	mtpRequestId requestId = 0;
	mtpTypeId type = 0;
	crl::time at = 0;
	bool suppressed = false;
};

// Leaked on purpose: the overlay hook appends to it until the process
// ends, past any static destruction order.
[[nodiscard]] std::vector<NotifyRequestRecord> &NotifyRequests() {
	static auto &result = *new std::vector<NotifyRequestRecord>();
	return result;
}

// The end of the request record right now.
[[nodiscard]] int RequestMark() {
	return int(NotifyRequests().size());
}

// The only read of the record: a window from a mark, never the whole
// history (test_probe.h states why).
[[nodiscard]] std::vector<NotifyRequestRecord> RequestsSince(int mark) {
	const auto &all = NotifyRequests();
	const auto from = std::clamp(mark, 0, int(all.size()));
	return std::vector<NotifyRequestRecord>(begin(all) + from, end(all));
}

using NotifyOverrideList = std::vector<std::shared_ptr<NotifyOverride>>;

// Every handle OverrideNotify returned, refused ones included, kept to
// the process end like the request record: a handle holds a weak session
// and a peer id only, so keeping it keeps nothing else alive.
[[nodiscard]] NotifyOverrideList &NotifyOverrides() {
	static auto &result = *new NotifyOverrideList();
	return result;
}

[[nodiscard]] bool &FinishRegistered() {
	static auto result = false;
	return result;
}

// WHY: access checking does not apply to the names in an explicit
// instantiation, so instantiating PrivateMember with the private member
// pointer defines Get(NotifyValueTag) returning it. Reading the unique_ptr
// through a const reference needs no complete NotifyPeerSettingsValue.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct NotifyValueTag {
	using type = std::unique_ptr<Data::NotifyPeerSettingsValue>
		Data::PeerNotifySettings::*;
	friend type Get(NotifyValueTag);
};

template struct PrivateMember<
	NotifyValueTag,
	&Data::PeerNotifySettings::_value>;

[[nodiscard]] QString BoolText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString OptionalText(const std::optional<bool> &value) {
	return value ? BoolText(*value) : u"unset"_q;
}

[[nodiscard]] QString SoundText(
		const std::optional<Data::NotifySound> &sound) {
	if (!sound) {
		return u"unset"_q;
	} else if (sound->none) {
		return u"none"_q;
	} else if (sound->id) {
		return u"ringtone:%1"_q.arg(sound->id);
	} else if (!sound->title.isEmpty() || !sound->data.isEmpty()) {
		return u"local:[%1][%2]"_q.arg(sound->title, sound->data);
	}
	return u"default"_q;
}

// The stored fields only, for a peer and for each default type alike.
void ReadStored(
		const Data::PeerNotifySettings &settings,
		PeerNotifyState &state) {
	state.read = true;
	state.known = !settings.settingsUnknown();
	state.valuePresent = ((settings.*Get(NotifyValueTag())) != nullptr);
	state.muteUntil = settings.muteUntil();
	state.sound = settings.sound();
	state.silentPosts = settings.silentPosts();
	const auto serialized = settings.serialize();
	const auto &data = serialized.data();
	if (const auto showPreviews = data.vshow_previews()) {
		state.showPreviews = mtpIsTrue(*showPreviews);
	}
	if (const auto storiesMuted = data.vstories_muted()) {
		state.storiesMuted = mtpIsTrue(*storiesMuted);
	}
}

[[nodiscard]] QString StoredText(const PeerNotifyState &state) {
	return u"known=%1 value=%2 mute_until=%3 other_sound=%4 silent=%5 "
		"show_previews=%6 stories_muted=%7"_q.arg(
			BoolText(state.known),
			BoolText(state.valuePresent),
			(state.muteUntil
				? QString::number(*state.muteUntil)
				: u"unset"_q),
			SoundText(state.sound),
			OptionalText(state.silentPosts),
			OptionalText(state.showPreviews),
			OptionalText(state.storiesMuted));
}

// The inverse of ParseSound (data_peer_notify_settings.cpp). Unlike the
// product's SerializeSound it keeps a local sound whose title is empty
// but whose data is not, so the round trip through change() is lossless.
[[nodiscard]] MTPNotificationSound SoundToMTP(
		const std::optional<Data::NotifySound> &sound) {
	return !sound
		? MTPNotificationSound()
		: sound->none
		? MTP_notificationSoundNone()
		: sound->id
		? MTP_notificationSoundRingtone(MTP_long(sound->id))
		: (!sound->title.isEmpty() || !sound->data.isEmpty())
		? MTP_notificationSoundLocal(
			MTP_string(sound->title),
			MTP_string(sound->data))
		: MTP_notificationSoundDefault();
}

// The served shape PeerNotifySettings::change(MTPPeerNotifySettings)
// turns back into |stored|'s stored fields. No flags at all makes the
// settings known with no value object (every field inherited from the
// defaults). A value object with no stored field is kept by one flag the
// client does not store, stories_hide_sender, because
// NotifyPeerSettingsValue::change sets all five fields from the data and
// an absent field to nullopt. "Unknown" has no served shape: a peer with
// unknown settings is refused before anything is applied.
[[nodiscard]] MTPPeerNotifySettings ServedShape(
		const PeerNotifyState &stored) {
	using Flag = MTPDpeerNotifySettings::Flag;
	const auto flag = [](const auto &optional, Flag bit) {
		return optional.has_value() ? bit : Flag(0);
	};
	auto flags = MTPDpeerNotifySettings::Flags(0);
	if (stored.valuePresent) {
		flags = flag(stored.showPreviews, Flag::f_show_previews)
			| flag(stored.silentPosts, Flag::f_silent)
			| flag(stored.muteUntil, Flag::f_mute_until)
			| flag(stored.sound, Flag::f_other_sound)
			| flag(stored.storiesMuted, Flag::f_stories_muted);
		if (!flags.value()) {
			flags = Flag::f_stories_hide_sender;
		}
	}
	return MTP_peerNotifySettings(
		MTP_flags(flags),
		MTP_bool(stored.showPreviews.value_or(true)), // show_previews
		MTP_bool(stored.silentPosts.value_or(false)), // silent
		MTP_int(stored.muteUntil.value_or(0)), // mute_until
		MTPNotificationSound(), // ios_sound
		MTPNotificationSound(), // android_sound
		SoundToMTP(stored.sound), // other_sound
		MTP_bool(stored.storiesMuted.value_or(false)), // stories_muted
		MTP_bool(false), // stories_hide_sender
		MTPNotificationSound(), // stories_ios_sound
		MTPNotificationSound(), // stories_android_sound
		MTPNotificationSound()); // stories_other_sound
}

// The only mutation path in this module: the exact call
// Api::Updates::feedUpdate makes for mtpc_updateNotifySettings. It
// reaches apply(PeerId), which needs peerLoaded; an unloaded peer always
// has unknown settings and is refused before this.
void ApplyLocally(
		not_null<PeerData*> peer,
		const MTPPeerNotifySettings &settings) {
	peer->owner().notifySettings().apply(
		MTP_notifyPeer(peerToMTP(peer->id)),
		settings);
}

[[nodiscard]] QString PeerIdText(PeerId id) {
	return QString::number(id.value);
}

// |captured| with every member |values| sets laid over it, always with a
// value object; a member left unset keeps the captured field and its
// presence, which ServedShape carries over as is.
[[nodiscard]] PeerNotifyState WithOverride(
		PeerNotifyState captured,
		const NotifyOverrideValues &values) {
	captured.valuePresent = true;
	if (values.muteUntil) {
		captured.muteUntil = values.muteUntil;
	}
	if (values.sound) {
		captured.sound = values.sound;
	}
	if (values.silentPosts) {
		captured.silentPosts = values.silentPosts;
	}
	if (values.showPreviews) {
		captured.showPreviews = values.showPreviews;
	}
	if (values.storiesMuted) {
		captured.storiesMuted = values.storiesMuted;
	}
	return captured;
}

// An applied, not yet restored override of |peer| in its own live
// session. A handle whose session is gone answers a null session() and
// never matches.
[[nodiscard]] bool LiveOverrideExists(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	return ranges::any_of(NotifyOverrides(), [&](const auto &handle) {
		return handle->applied()
			&& !handle->restored()
			&& (handle->session() == session)
			&& (handle->peerId() == peer->id);
	});
}

// The constructor word, read in place: the hook sees every request of the
// process, so nothing is copied.
[[nodiscard]] mtpTypeId PeekConstructor(
		const MTP::details::SerializedRequest &request) {
	constexpr auto kBody
		= MTP::details::SerializedRequest::kMessageBodyPosition;
	return (request && (request->size() > kBody))
		? mtpTypeId(request->constData()[kBody])
		: mtpTypeId(0);
}

// The locality stage's |then|: sends the control and decides in the same
// main-thread turn, so the control's record is already in the window.
void CheckNotifyLocality(
		const std::shared_ptr<NotifyOverride> &handle,
		const QString &what,
		const QString &claim) {
	if (!handle || !handle->restored()) {
		Fail(
			what + u": restore before the locality check"_q,
			u"handle=%1 restored=0 - the tail is measured from the last "
			"restore, so a live override has no locality reading"_q.arg(
				handle ? PeerIdText(handle->peerId()) : u"null"_q));
		return;
	}
	const auto session = handle->session();
	if (!session) {
		Fail(
			what + u": the session is gone, so no control can be sent"_q,
			u"peer=%1 lastRestoreAt=%2"_q.arg(
				PeerIdText(handle->peerId()),
				QString::number(handle->lastRestoreAt())));
		return;
	}
	const auto firstMark = handle->firstRequestMark();
	const auto controlAt = crl::now();

	// Raw and handler-less on purpose: ApiWrap::requestNotifySettings
	// would apply the answer and de-duplicate by key. send() reaches the
	// hook synchronously, so the control is recorded before it returns.
	const auto controlId = session->api().request(
		MTPaccount_GetNotifySettings(MTP_inputNotifyUsers())
	).send();
	const auto endMark = RequestMark();
	auto scan = DiscriminatingScan(
		what,
		u"requests formed from the first override mark carrying "
		"account.updateNotifySettings"_q,
		u"the deliberate account.getNotifySettings control r%1"_q.arg(
			controlId));
	auto subjects = QStringList();
	auto productReads = QStringList();
	for (const auto &record : RequestsSince(firstMark)) {
		scan.examined();
		if (record.type == mtpc_account_updateNotifySettings) {
			const auto detail = u"r%1 at=%2 suppressed=%3"_q.arg(
				QString::number(record.requestId),
				QString::number(record.at),
				BoolText(record.suppressed));
			scan.matchedSubject(detail);
			subjects.push_back(detail);
		} else if (record.type == mtpc_account_getNotifySettings) {
			if (record.requestId == controlId) {
				scan.matchedControl(u"r%1"_q.arg(controlId));
			} else {
				productReads.push_back(u"r%1"_q.arg(record.requestId));
			}
		}
	}
	const auto decided = scan.report();
	const auto tail = controlAt - handle->lastRestoreAt();
	Check(
		decided && (scan.subjectCount() == 0) && (tail > kNotifySaveTimeout),
		claim,
		u"decided=%1 subjects=%2 [%3] control=r%4 controls=%5 "
		"otherNotifyReads=%6 [%7]"_q.arg(
			BoolText(decided),
			QString::number(scan.subjectCount()),
			subjects.join(u", "_q),
			QString::number(controlId),
			QString::number(scan.controlCount()),
			QString::number(productReads.size()),
			productReads.join(u", "_q))
		+ u" peer=%1 firstMark=%2 end=%3 examined=%4 appliedAt=%5 "
		"lastRestoreAt=%6 controlAt=%7 tailMs=%8 saveTimeoutMs=%9"_q.arg(
			PeerIdText(handle->peerId()),
			QString::number(firstMark),
			QString::number(endMark),
			QString::number(scan.examinedCount()),
			QString::number(handle->appliedAt()),
			QString::number(handle->lastRestoreAt()),
			QString::number(controlAt),
			QString::number(tail),
			QString::number(kNotifySaveTimeout)));
}

// The self-test's helpers. Each reads the live session and keeps nothing.

[[nodiscard]] PeerData *LoadedPeer(Main::Session *session, PeerId id) {
	return session ? session->data().peerLoaded(id) : nullptr;
}

// A subject the self-test can override and read back: loaded (the apply
// reaches only a loaded peer), known settings, a decided mute and a loaded
// history, whose cached muted() flag is one of the readers checked.
[[nodiscard]] bool FixturePeerReady(
		not_null<Main::Session*> session,
		PeerId id) {
	const auto peer = session->data().peerLoaded(id);
	return peer
		&& !peer->notify().settingsUnknown()
		&& !session->data().notifySettings().muteUnknown(peer)
		&& (session->data().historyLoaded(id) != nullptr);
}

[[nodiscard]] QString FixturePeerText(
		not_null<Main::Session*> session,
		PeerId id) {
	const auto peer = session->data().peerLoaded(id);
	if (!peer) {
		return u"peer=%1 loaded=0"_q.arg(PeerIdText(id));
	}
	return u"peer=%1 loaded=1 history=%2 %3"_q.arg(
		PeerIdText(id),
		BoolText(session->data().historyLoaded(id) != nullptr),
		PeerNotifyStateText(ReadPeerNotifyState(peer)));
}

// The other peer's whole reading, compared as text before and after.
[[nodiscard]] QString OtherPeerText(
		not_null<Main::Session*> session,
		PeerId id) {
	if (!id) {
		return u"none"_q;
	}
	const auto peer = session->data().peerLoaded(id);
	return peer
		? u"peer=%1 %2"_q.arg(
			PeerIdText(id),
			PeerNotifyStateText(ReadPeerNotifyState(peer)))
		: u"peer=%1 not loaded"_q.arg(PeerIdText(id));
}

[[nodiscard]] QString PeerKindText(not_null<PeerData*> peer) {
	return peer->isUser()
		? u"user"_q
		: (peer->isChat() || peer->isMegagroup())
		? u"group"_q
		: u"broadcast"_q;
}

// The product's three readers of whether a peer is muted: isMuted(peer),
// isMuted(history) and the history's cached Data::Thread::muted(), the
// last two -1 without a loaded history.
struct MutedReaders {
	bool peer = false;
	int thread = -1;
	int history = -1;
};

[[nodiscard]] MutedReaders ReadMutedReaders(not_null<PeerData*> peer) {
	const auto &owner = peer->owner().notifySettings();
	auto result = MutedReaders{ .peer = owner.isMuted(peer) };
	if (const auto history = peer->owner().historyLoaded(peer->id)) {
		result.thread = owner.isMuted(history) ? 1 : 0;
		result.history = history->muted() ? 1 : 0;
	}
	return result;
}

[[nodiscard]] bool ReadersAre(const MutedReaders &readers, bool muted) {
	const auto value = muted ? 1 : 0;
	return (readers.peer == muted)
		&& (readers.thread == value)
		&& (readers.history == value);
}

[[nodiscard]] QString MutedReadersText(const MutedReaders &readers) {
	return u"isMuted(peer)=%1 isMuted(history)=%2 history->muted()=%3"_q.arg(
		BoolText(readers.peer),
		QString::number(readers.thread),
		QString::number(readers.history));
}

// The stored fields with value presence, without the derived readings.
[[nodiscard]] bool SameStored(
		const PeerNotifyState &a,
		const PeerNotifyState &b) {
	return (a.known == b.known)
		&& (a.valuePresent == b.valuePresent)
		&& (a.muteUntil == b.muteUntil)
		&& (a.sound == b.sound)
		&& (a.silentPosts == b.silentPosts)
		&& (a.showPreviews == b.showPreviews)
		&& (a.storiesMuted == b.storiesMuted);
}

struct UnknownPeers {
	std::vector<not_null<PeerData*>> list;
	int examined = 0;
};

// Every peer the session already holds whose notify settings are still
// unknown, except |excluded|: loaded ones first, because apply(PeerId)
// would really change a loaded one, so only its refusal shows the gate
// doing the work; then by id. The walk reads the existing peers only -
// nothing is created or requested - and the pointers live for the caller's
// main-thread turn only.
[[nodiscard]] UnknownPeers CollectUnknownPeers(
		not_null<Main::Session*> session,
		const std::vector<PeerId> &excluded) {
	auto result = UnknownPeers();
	const auto consider = [&](not_null<PeerData*> peer) {
		++result.examined;
		if (peer->notify().settingsUnknown()
			&& !ranges::contains(excluded, peer->id)) {
			result.list.push_back(peer);
		}
	};
	const auto &data = session->data();
	data.enumerateUsers([&](not_null<UserData*> user) {
		consider(user);
	});
	data.enumerateGroups([&](not_null<PeerData*> peer) {
		consider(peer);
	});
	data.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
		consider(channel);
	});
	ranges::sort(result.list, [](
			not_null<PeerData*> a,
			not_null<PeerData*> b) {
		const auto aLoaded = a->isLoaded();
		const auto bLoaded = b->isLoaded();
		return (aLoaded != bLoaded) ? aLoaded : (a->id < b->id);
	});
	return result;
}

} // namespace

PeerNotifyState ReadPeerNotifyState(not_null<PeerData*> peer) {
	auto result = PeerNotifyState();
	ReadStored(peer->notify(), result);
	const auto &owner = peer->owner().notifySettings();
	result.muteUnknown = owner.muteUnknown(peer);
	result.muted = owner.isMuted(peer);
	if (const auto history = peer->owner().historyLoaded(peer->id)) {
		result.historyMuted = history->muted() ? 1 : 0;
	}
	const auto &exceptions = owner.exceptions(Data::DefaultNotifyType(peer));
	result.exception = exceptions.contains(peer) ? 1 : 0;
	return result;
}

QString PeerNotifyStateText(const PeerNotifyState &state) {
	if (!state.read) {
		return u"unread"_q;
	}
	return StoredText(state)
		+ u" muteUnknown=%1 muted=%2 historyMuted=%3 exception=%4"_q.arg(
			BoolText(state.muteUnknown),
			BoolText(state.muted),
			QString::number(state.historyMuted),
			QString::number(state.exception));
}

QString DefaultNotifyStateText(not_null<Main::Session*> session) {
	const auto &owner = session->data().notifySettings();
	const auto text = [&](Data::DefaultNotify type) {
		auto state = PeerNotifyState();
		ReadStored(owner.defaultSettings(type), state);
		return StoredText(state)
			+ u" muted=%1"_q.arg(BoolText(owner.isMuted(type)));
	};
	return u"user{%1} group{%2} broadcast{%3}"_q.arg(
		text(Data::DefaultNotify::User),
		text(Data::DefaultNotify::Group),
		text(Data::DefaultNotify::Broadcast));
}

NotifyOverrideValues UnmutedSilentNotify() {
	return {
		.muteUntil = TimeId(0),
		.sound = Data::NotifySound{ .none = true },
	};
}

NotifyOverrideValues MutedSilentNotify() {
	return {
		.muteUntil = std::numeric_limits<TimeId>::max(),
		.sound = Data::NotifySound{ .none = true },
	};
}

QString NotifyOverrideGateName(NotifyOverrideGate gate) {
	switch (gate) {
	case NotifyOverrideGate::None:
		return u"none"_q;
	case NotifyOverrideGate::SettingsUnknown:
		return u"settings-unknown"_q;
	case NotifyOverrideGate::AlreadyOverridden:
		return u"already-overridden"_q;
	}
	return u"missing"_q;
}

bool NotifyRestoreReading::restored() const {
	return (refused != NotifyOverrideGate::None) || equal || sessionGone;
}

QString NotifyRestoreText(const NotifyRestoreReading &reading) {
	if (reading.refused != NotifyOverrideGate::None) {
		return u"refused=%1: nothing was applied, nothing to restore"_q.arg(
			NotifyOverrideGateName(reading.refused));
	} else if (reading.sessionGone) {
		return u"session=gone: the override ended with the session"_q;
	}
	return u"applied=%1 intact=%2 equal=%3 captured{%4} now{%5}"_q.arg(
		BoolText(reading.applied),
		reading.applied ? BoolText(reading.intact) : u"-"_q,
		BoolText(reading.equal),
		reading.captured,
		reading.now);
}

bool CheckNotifyRestored(
		const NotifyRestoreReading &reading,
		const QString &what) {
	const auto ok = reading.restored();
	Check(ok, what, NotifyRestoreText(reading));
	return ok;
}

bool NotifyOverride::applied() const {
	return _applied;
}

NotifyOverrideGate NotifyOverride::gate() const {
	return _gate;
}

PeerId NotifyOverride::peerId() const {
	return _peerId;
}

Main::Session *NotifyOverride::session() const {
	return _session.get();
}

const PeerNotifyState &NotifyOverride::captured() const {
	return _captured;
}

const PeerNotifyState &NotifyOverride::overridden() const {
	return _overridden;
}

int NotifyOverride::firstRequestMark() const {
	return _requestMark;
}

crl::time NotifyOverride::appliedAt() const {
	return _appliedAt;
}

crl::time NotifyOverride::lastRestoreAt() const {
	return _lastRestoreAt;
}

bool NotifyOverride::restored() const {
	return _restored;
}

NotifyRestoreReading NotifyOverride::restore() {
	_lastRestoreAt = crl::now();
	auto result = NotifyRestoreReading{
		.refused = _gate,
		.captured = PeerNotifyStateText(_captured),
	};
	if (_gate != NotifyOverrideGate::None) {
		return result;
	}
	const auto session = _session.get();
	if (!session) {
		result.sessionGone = true;
		return result;
	}
	const auto peer = session->data().peerLoaded(_peerId);
	if (!peer) {
		result.now = u"peer not loaded"_q;
		return result;
	}
	if (!_restored) {
		// Reported, never fought: a served writer that replaced the
		// override inside the window reads intact=0, and the capture is
		// applied back all the same.
		result.intact = (ReadPeerNotifyState(peer) == _overridden);
		ApplyLocally(peer, ServedShape(_captured));
		_restored = true;
		result.applied = true;
	}
	const auto now = ReadPeerNotifyState(peer);
	result.equal = (now == _captured);
	result.now = PeerNotifyStateText(now);
	return result;
}

std::shared_ptr<NotifyOverride> OverrideNotify(
		not_null<Runner*> runner,
		not_null<PeerData*> peer,
		const NotifyOverrideValues &values) {
	auto result = std::shared_ptr<NotifyOverride>(new NotifyOverride());
	result->_session = base::make_weak(&peer->session());
	result->_peerId = peer->id;
	result->_captured = ReadPeerNotifyState(peer);
	result->_gate = peer->notify().settingsUnknown()
		? NotifyOverrideGate::SettingsUnknown
		: LiveOverrideExists(peer)
		? NotifyOverrideGate::AlreadyOverridden
		: NotifyOverrideGate::None;
	if (result->_gate == NotifyOverrideGate::None) {
		const auto served = ServedShape(
			WithOverride(result->_captured, values));

		// The mark is taken immediately before the apply, so the locality
		// window starts with whatever the apply itself could form.
		result->_requestMark = RequestMark();
		ApplyLocally(peer, served);
		result->_appliedAt = crl::now();
		result->_overridden = ReadPeerNotifyState(peer);
		result->_applied = true;
		Note(u"notify override: peer %1 overridden through the local apply, "
			"requestMark=%2 captured{%3} overridden{%4}"_q.arg(
				PeerIdText(peer->id),
				QString::number(result->_requestMark),
				PeerNotifyStateText(result->_captured),
				PeerNotifyStateText(result->_overridden)));
	} else {
		// A Note rather than a Fail: the caller decides between a fixture
		// gate and a FAIL, and the refusal is in the log either way.
		Note(u"notify override: peer %1 refused by %2, nothing applied; "
			"captured{%3}"_q.arg(
				PeerIdText(peer->id),
				NotifyOverrideGateName(result->_gate),
				PeerNotifyStateText(result->_captured)));
	}
	NotifyOverrides().push_back(result);

	if (!FinishRegistered()) {
		FinishRegistered() = true;

		// One registration for the process, walking LIFO: Runner::finish()
		// runs its callbacks FIFO, so one callback per handle would unwind
		// nested overrides outermost first. A handle the campaign already
		// restored only verifies, so this is safe after a teardown stage.
		runner->onFinish([] {
			const auto &overrides = NotifyOverrides();
			for (auto i = rbegin(overrides); i != rend(overrides); ++i) {
				const auto &handle = *i;
				if (!handle->applied()) {
					continue;
				}
				CheckNotifyRestored(
					handle->restore(),
					u"notify override: onFinish: notify settings of peer "
					"%1 restored to the captured values"_q.arg(
						PeerIdText(handle->peerId())));
			}
		});
	}
	return result;
}

bool InterceptNotifyRequest(
		const MTP::details::SerializedRequest &request) {
	Expects(QThread::currentThread() == QCoreApplication::instance()->thread());

	const auto type = PeekConstructor(request);
	const auto suppressed = Active()
		&& (type == mtpc_account_updateNotifySettings);
	NotifyRequests().push_back({
		.requestId = request ? request->requestId : mtpRequestId(0),
		.type = type,
		.at = crl::now(),
		.suppressed = suppressed,
	});
	return suppressed;
}

void AppendNotifyLocalityCheck(
		not_null<Runner*> runner,
		Fn<std::shared_ptr<NotifyOverride>()> resolve,
		const QString &what) {
	const auto claim = what
		+ u": no account.updateNotifySettings from the first override "
		"until more than 1 s after the last restore"_q;
	runner->add({
		.name = claim,
		.skipReason = [=] {
			const auto handle = resolve();
			return (!handle || !handle->applied())
				? u"no applied override"_q
				: (RequestMark() == 0)
				? u"observer hook not installed: no request reached "
				"Test::InterceptNotifyRequest since launch"_q
				: QString();
		},
		.until = [=] {
			const auto handle = resolve();
			return !handle
				|| !handle->restored()
				|| ((crl::now() - handle->lastRestoreAt())
					> kNotifyLocalityTail);
		},
		.then = [=] {
			CheckNotifyLocality(resolve(), what, claim);
		},
		.timeoutDetails = [=] {
			const auto handle = resolve();
			if (!handle) {
				return u"handle=null"_q;
			}
			const auto now = crl::now();
			return u"peer=%1 restored=%2 lastRestoreAt=%3 now=%4 "
				"sinceRestoreMs=%5 tailMs=%6"_q.arg(
					PeerIdText(handle->peerId()),
					BoolText(handle->restored()),
					QString::number(handle->lastRestoreAt()),
					QString::number(now),
					QString::number(now - handle->lastRestoreAt()),
					QString::number(kNotifyLocalityTail));
		},
	});
}

void AppendNotifyOverrideSelfTest(not_null<Runner*> runner) {
	struct State {
		base::weak_ptr<Main::Session> weak;
		PeerId subjectId = 0;
		PeerId otherId = 0;
		QString subjectSource;
		PeerNotifyState served;
		NotifyOverrideValues values;
		QString valuesName;
		QString otherBefore;
		QString defaultsBefore;
		std::shared_ptr<NotifyOverride> handle;
		QString fixtureGate;
		crl::time resolveStartedAt = 0;
	};
	// Kept alive by the stages that capture it. It holds the session only
	// weakly and the peers only by id: every stage resolves them again.
	const auto state = std::make_shared<State>();

	const auto fixtureGate = [=] {
		return state->fixtureGate;
	};
	const auto overrideGate = [=] {
		return !state->fixtureGate.isEmpty()
			? state->fixtureGate
			: (!state->handle || !state->handle->applied())
			? u"the override stage applied nothing; its rows say why"_q
			: QString();
	};
	const auto resolveSubject = [=] {
		return LoadedPeer(state->weak.get(), state->subjectId);
	};
	const auto subjectGone = [=](const QString &what) {
		Fail(
			what,
			u"session=%1 subject=%2: the subject peer is not loaded"_q.arg(
				state->weak ? u"alive"_q : u"gone"_q,
				PeerIdText(state->subjectId)));
	};
	const auto checkOthers = [=](const QString &when) {
		const auto session = state->weak.get();
		const auto otherNow = session
			? OtherPeerText(session, state->otherId)
			: u"session=gone"_q;
		const auto defaultsNow = session
			? DefaultNotifyStateText(session)
			: u"session=gone"_q;
		Check(
			(otherNow == state->otherBefore)
				&& (defaultsNow == state->defaultsBefore),
			u"notify override self-test: %1: the other peer and the default "
			"settings are unchanged"_q.arg(when),
			u"other before{%1} now{%2} defaults before{%3} now{%4}"_q.arg(
				state->otherBefore,
				otherNow,
				state->defaultsBefore,
				defaultsNow));
	};

	runner->waitForSessionReady();

	runner->add({
		.name = u"notify override self-test: resolve the fixture peers"_q,
		.run = [=] {
			state->resolveStartedAt = crl::now();
			const auto &account = Core::App().domain().active();
			if (account.sessionExists()) {
				state->weak = base::make_weak(&account.session());
			}
		},
		.until = [=] {
			const auto session = state->weak.get();
			return !session
				|| FixturePeerReady(
					session,
					PeerData::kServiceNotificationsId)
				|| (session->data().chatsListLoaded()
					&& FixturePeerReady(session, session->userPeerId()))
				|| ((crl::now() - state->resolveStartedAt)
					>= kStartupStageTimeout);
		},
		.then = [=] {
			const auto session = state->weak.get();
			if (!session) {
				state->fixtureGate = u"fixture gate: no active session"_q;
				return;
			}
			const auto service = PeerData::kServiceNotificationsId;
			const auto self = session->userPeerId();
			const auto serviceReady = FixturePeerReady(session, service);
			if (!serviceReady && !FixturePeerReady(session, self)) {
				state->fixtureGate = u"fixture gate: neither 777000 nor "
					"Saved Messages has known notify settings and a loaded "
					"history; 777000{%1} self{%2}"_q.arg(
						FixturePeerText(session, service),
						FixturePeerText(session, self));
				return;
			}
			state->subjectId = serviceReady ? service : self;
			state->subjectSource = serviceReady
				? u"777000"_q
				: u"saved-messages"_q;
			const auto otherId = serviceReady ? self : service;
			state->otherId = session->data().peerLoaded(otherId)
				? otherId
				: PeerId();
			const auto subject = session->data().peerLoaded(state->subjectId);
			state->served = ReadPeerNotifyState(subject);
			state->values = state->served.muted
				? UnmutedSilentNotify()
				: MutedSilentNotify();
			state->valuesName = state->served.muted
				? u"UnmutedSilentNotify"_q
				: u"MutedSilentNotify"_q;
			state->otherBefore = OtherPeerText(session, state->otherId);
			state->defaultsBefore = DefaultNotifyStateText(session);
			Note(u"notify override self-test: subject=%1 (%2) values=%3 "
				"other=%4 elapsedMs=%5 served{%6} other{%7} defaults{%8}"_q.arg(
					PeerIdText(state->subjectId),
					state->subjectSource,
					state->valuesName,
					PeerIdText(state->otherId),
					QString::number(crl::now() - state->resolveStartedAt),
					PeerNotifyStateText(state->served),
					state->otherBefore,
					state->defaultsBefore));
		},
		.timeout = kStartupStageTimeout + crl::time(5000),
		.timeoutDetails = [=] {
			const auto session = state->weak.get();
			if (!session) {
				return u"session=gone"_q;
			}
			return u"777000{%1} self{%2} chatsListLoaded=%3"_q.arg(
				FixturePeerText(session, PeerData::kServiceNotificationsId),
				FixturePeerText(session, session->userPeerId()),
				BoolText(session->data().chatsListLoaded()));
		},
	});

	runner->add({
		.name = u"notify override self-test: override the subject, "
			"inverting its served isMuted"_q,
		.skipReason = fixtureGate,
		.then = [=] {
			const auto applied = u"notify override self-test: the override "
				"is applied"_q;
			const auto subject = resolveSubject();
			if (!subject) {
				subjectGone(applied);
				return;
			}
			state->handle = OverrideNotify(runner, subject, state->values);
			const auto &handle = state->handle;
			Check(
				handle->applied()
					&& (handle->gate() == NotifyOverrideGate::None),
				applied,
				u"peer=%1 (%2) values=%3 gate=%4 applied=%5 requestMark=%6 "
				"appliedAt=%7"_q.arg(
					PeerIdText(state->subjectId),
					state->subjectSource,
					state->valuesName,
					NotifyOverrideGateName(handle->gate()),
					BoolText(handle->applied()),
					QString::number(handle->firstRequestMark()),
					QString::number(handle->appliedAt())));
			if (!handle->applied()) {
				// Every reading below would measure the refusal, not the
				// override: the row above already names why.
				return;
			}
			const auto now = ReadPeerNotifyState(subject);
			const auto readers = ReadMutedReaders(subject);
			Check(
				ReadersAre(readers, !state->served.muted),
				u"notify override self-test: the product's readers report "
				"the override"_q,
				u"expectedMuted=%1 %2 served{%3} now{%4}"_q.arg(
					BoolText(!state->served.muted),
					MutedReadersText(readers),
					PeerNotifyStateText(state->served),
					PeerNotifyStateText(now)));
			const auto requested = WithOverride(
				handle->captured(),
				state->values);
			Check(
				SameStored(now, requested),
				u"notify override self-test: the stored override fields are "
				"the requested ones and every other field is the captured "
				"one"_q,
				u"requested{%1} now{%2} captured{%3}"_q.arg(
					StoredText(requested),
					StoredText(now),
					StoredText(handle->captured())));
			Check(
				handle->overridden() != handle->captured(),
				u"notify override self-test: the restore comparator tells "
				"the override from the capture"_q,
				u"captured{%1} overridden{%2}"_q.arg(
					PeerNotifyStateText(handle->captured()),
					PeerNotifyStateText(handle->overridden())));
			checkOthers(u"override"_q);
		},
	});

	runner->add({
		.name = u"notify override self-test: refusals inside the window"_q,
		.skipReason = overrideGate,
		.then = [=] {
			const auto nested = u"notify override self-test: a second "
				"override of the live subject is refused by name, nothing "
				"applied"_q;
			const auto session = state->weak.get();
			const auto subject = resolveSubject();
			if (!session || !subject) {
				subjectGone(nested);
				return;
			}
			const auto &handle = state->handle;
			const auto second = OverrideNotify(runner, subject, state->values);
			const auto subjectNow = ReadPeerNotifyState(subject);
			Check(
				(second->gate() == NotifyOverrideGate::AlreadyOverridden)
					&& !second->applied()
					&& (subjectNow == handle->overridden()),
				nested,
				u"gate=%1 applied=%2 overridden{%3} now{%4}"_q.arg(
					NotifyOverrideGateName(second->gate()),
					BoolText(second->applied()),
					PeerNotifyStateText(handle->overridden()),
					PeerNotifyStateText(subjectNow)));

			// Picked and overridden in this one turn: no
			// account.getNotifySettings answer can land in between and
			// make the chosen peer's settings known.
			const auto unknownClaim = u"notify override self-test: a peer "
				"with unknown notify settings is refused by name, nothing "
				"applied"_q;
			const auto candidates = CollectUnknownPeers(
				session,
				{ state->subjectId, state->otherId });
			if (candidates.list.empty()) {
				Skipped(
					unknownClaim,
					u"fixture gate: no peer with unknown notify settings "
					"(examined %1)"_q.arg(candidates.examined));
				return;
			}
			const auto unknown = candidates.list.front();
			const auto before = ReadPeerNotifyState(unknown);
			const auto refused = OverrideNotify(
				runner,
				unknown,
				UnmutedSilentNotify());
			const auto after = ReadPeerNotifyState(unknown);
			const auto otherUnchanged = (OtherPeerText(session, state->otherId)
				== state->otherBefore);
			const auto defaultsUnchanged = (DefaultNotifyStateText(session)
				== state->defaultsBefore);
			const auto subjectStill = (ReadPeerNotifyState(subject)
				== handle->overridden());
			Check(
				(refused->gate() == NotifyOverrideGate::SettingsUnknown)
					&& !refused->applied()
					&& (after == before)
					&& !after.known
					&& !after.valuePresent
					&& otherUnchanged
					&& defaultsUnchanged
					&& subjectStill,
				unknownClaim,
				u"peer=%1 kind=%2 isLoaded=%3 candidates=%4 examined=%5 "
				"gate=%6 applied=%7 before{%8} after{%9}"_q.arg(
					PeerIdText(unknown->id),
					PeerKindText(unknown),
					BoolText(unknown->isLoaded()),
					QString::number(int(candidates.list.size())),
					QString::number(candidates.examined),
					NotifyOverrideGateName(refused->gate()),
					BoolText(refused->applied()),
					PeerNotifyStateText(before),
					PeerNotifyStateText(after))
				+ u" otherUnchanged=%1 defaultsUnchanged=%2 "
				"subjectStillOverridden=%3"_q.arg(
					BoolText(otherUnchanged),
					BoolText(defaultsUnchanged),
					BoolText(subjectStill)));
		},
	});

	runner->add({
		.name = u"notify override self-test: restore at the window's end"_q,
		.skipReason = overrideGate,
		.then = [=] {
			const auto reading = state->handle->restore();
			CheckNotifyRestored(
				reading,
				u"notify override self-test: the restore makes the whole "
				"stored state equal to the capture"_q);
			Check(
				reading.applied && reading.intact,
				u"notify override self-test: the first restore applied the "
				"capture back over an intact override"_q,
				NotifyRestoreText(reading));
			const auto back = u"notify override self-test: the product's "
				"readers are back to the served value"_q;
			const auto subject = resolveSubject();
			if (!subject) {
				subjectGone(back);
				return;
			}
			const auto readers = ReadMutedReaders(subject);
			Check(
				ReadersAre(readers, state->served.muted),
				back,
				u"expectedMuted=%1 %2 served{%3}"_q.arg(
					BoolText(state->served.muted),
					MutedReadersText(readers),
					PeerNotifyStateText(state->served)));
			checkOthers(u"restore"_q);
		},
	});

	runner->add({
		.name = u"notify override self-test: teardown: a second restore "
			"applies nothing"_q,
		.skipReason = overrideGate,
		.then = [=] {
			const auto reading = state->handle->restore();
			Check(
				!reading.applied && reading.equal,
				u"notify override self-test: teardown: a second restore "
				"applies nothing and still reads equal"_q,
				NotifyRestoreText(reading));
			checkOthers(u"teardown"_q);
		},
	});

	AppendNotifyLocalityCheck(
		runner,
		[=] { return state->handle; },
		u"notify override self-test: locality"_q);
}

} // namespace Test

#endif // _DEBUG
