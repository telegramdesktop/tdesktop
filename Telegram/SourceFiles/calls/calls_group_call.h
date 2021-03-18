/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "mtproto/sender.h"
#include "mtproto/mtproto_auth_key.h"

class History;

namespace tgcalls {
class GroupInstanceCustomImpl;
struct GroupLevelsUpdate;
struct GroupNetworkState;
struct GroupParticipantDescription;
} // namespace tgcalls

namespace base {
class GlobalShortcutManager;
class GlobalShortcutValue;
} // namespace base

namespace Webrtc {
class MediaDevices;
} // namespace Webrtc

namespace Data {
struct LastSpokeTimes;
struct GroupCallParticipant;
} // namespace Data

namespace Calls {

namespace Group {
struct MuteRequest;
struct VolumeRequest;
struct ParticipantState;
struct JoinInfo;
struct RejoinEvent;
} // namespace Group

enum class MuteState {
	Active,
	PushToTalk,
	Muted,
	ForceMuted,
	RaisedHand,
};

[[nodiscard]] inline auto MapPushToTalkToActive() {
	return rpl::map([=](MuteState state) {
		return (state == MuteState::PushToTalk) ? MuteState::Active : state;
	});
}

[[nodiscard]] bool IsGroupCallAdmin(
	not_null<PeerData*> peer,
	not_null<PeerData*> participantPeer);

struct LevelUpdate {
	uint32 ssrc = 0;
	float value = 0.;
	bool voice = false;
	bool me = false;
};

class GroupCall final : public base::has_weak_ptr {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;

		virtual void groupCallFinished(not_null<GroupCall*> call) = 0;
		virtual void groupCallFailed(not_null<GroupCall*> call) = 0;
		virtual void groupCallRequestPermissionsOrFail(
			Fn<void()> onSuccess) = 0;

		enum class GroupCallSound {
			Started,
			Connecting,
			AllowedToSpeak,
			Ended,
		};
		virtual void groupCallPlaySound(GroupCallSound sound) = 0;
	};

	using GlobalShortcutManager = base::GlobalShortcutManager;

	GroupCall(
		not_null<Delegate*> delegate,
		Group::JoinInfo info,
		const MTPInputGroupCall &inputCall);
	~GroupCall();

	[[nodiscard]] uint64 id() const {
		return _id;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] not_null<PeerData*> joinAs() const {
		return _joinAs;
	}
	[[nodiscard]] bool showChooseJoinAs() const;

	void start();
	void hangup();
	void discard();
	void rejoinAs(Group::JoinInfo info);
	void rejoinWithHash(const QString &hash);
	void join(const MTPInputGroupCall &inputCall);
	void handleUpdate(const MTPUpdate &update);
	void handlePossibleCreateOrJoinResponse(const MTPDupdateGroupCall &data);
	void changeTitle(const QString &title);
	void toggleRecording(bool enabled, const QString &title);
	[[nodiscard]] bool recordingStoppedByMe() const {
		return _recordingStoppedByMe;
	}

	void setMuted(MuteState mute);
	void setMutedAndUpdate(MuteState mute);
	[[nodiscard]] MuteState muted() const {
		return _muted.current();
	}
	[[nodiscard]] rpl::producer<MuteState> mutedValue() const {
		return _muted.value();
	}

	[[nodiscard]] auto otherParticipantStateValue() const
		-> rpl::producer<Group::ParticipantState>;

	enum State {
		Creating,
		Joining,
		Connecting,
		Joined,
		FailedHangingUp,
		Failed,
		HangingUp,
		Ended,
	};
	[[nodiscard]] State state() const {
		return _state.current();
	}
	[[nodiscard]] rpl::producer<State> stateValue() const {
		return _state.value();
	}

	enum class InstanceState {
		Disconnected,
		TransitionToRtc,
		Connected,
	};
	[[nodiscard]] InstanceState instanceState() const {
		return _instanceState.current();
	}
	[[nodiscard]] rpl::producer<InstanceState> instanceStateValue() const {
		return _instanceState.value();
	}

	[[nodiscard]] rpl::producer<LevelUpdate> levelUpdates() const {
		return _levelUpdates.events();
	}
	[[nodiscard]] rpl::producer<Group::RejoinEvent> rejoinEvents() const {
		return _rejoinEvents.events();
	}
	[[nodiscard]] rpl::producer<> allowedToSpeakNotifications() const {
		return _allowedToSpeakNotifications.events();
	}
	[[nodiscard]] rpl::producer<> titleChanged() const {
		return _titleChanged.events();
	}
	static constexpr auto kSpeakLevelThreshold = 0.2;

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	//void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

	void toggleMute(const Group::MuteRequest &data);
	void changeVolume(const Group::VolumeRequest &data);
	std::variant<int, not_null<UserData*>> inviteUsers(
		const std::vector<not_null<UserData*>> &users);

	std::shared_ptr<GlobalShortcutManager> ensureGlobalShortcutManager();
	void applyGlobalShortcutChanges();

	void pushToTalk(bool pressed, crl::time delay);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class LoadPartTask;

public:
	void broadcastPartStart(std::shared_ptr<LoadPartTask> task);
	void broadcastPartCancel(not_null<LoadPartTask*> task);

private:
	using GlobalShortcutValue = base::GlobalShortcutValue;

	struct LoadingPart {
		std::shared_ptr<LoadPartTask> task;
		mtpRequestId requestId = 0;
	};

	enum class FinishType {
		None,
		Ended,
		Failed,
	};
	enum class InstanceMode {
		None,
		Rtc,
		Stream,
	};
	enum class SendUpdateType {
		Mute,
		RaiseHand,
	};

	void handlePossibleCreateOrJoinResponse(const MTPDgroupCall &data);
	void handlePossibleDiscarded(const MTPDgroupCallDiscarded &data);
	void handleUpdate(const MTPDupdateGroupCall &data);
	void handleUpdate(const MTPDupdateGroupCallParticipants &data);
	void handleRequestError(const MTP::Error &error);
	void handleControllerError(const QString &error);
	void ensureControllerCreated();
	void destroyController();

	void setState(State state);
	void finish(FinishType type);
	void maybeSendMutedUpdate(MuteState previous);
	void sendSelfUpdate(SendUpdateType type);
	void updateInstanceMuteState();
	void updateInstanceVolumes();
	void applyMeInCallLocally();
	void rejoin();
	void rejoin(not_null<PeerData*> as);

	void audioLevelsUpdated(const tgcalls::GroupLevelsUpdate &data);
	void setInstanceConnected(tgcalls::GroupNetworkState networkState);
	void setInstanceMode(InstanceMode mode);
	void checkLastSpoke();
	void pushToTalkCancel();

	void checkGlobalShortcutAvailability();
	void checkJoined();
	void checkFirstTimeJoined();
	void notifyAboutAllowedToSpeak();

	void playConnectingSound();
	void stopConnectingSound();
	void playConnectingSoundOnce();

	void requestParticipantsInformation(const std::vector<uint32_t> &ssrcs);
	void addParticipantsToInstance();
	void prepareParticipantForAdding(
		const Data::GroupCallParticipant &participant);
	void addPreparedParticipants();
	void addPreparedParticipantsDelayed();

	void editParticipant(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);
	void applyParticipantLocally(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	not_null<PeerData*> _peer; // Can change in legacy group migration.
	rpl::event_stream<PeerData*> _peerStream;
	not_null<History*> _history; // Can change in legacy group migration.
	MTP::Sender _api;
	rpl::variable<State> _state = State::Creating;
	rpl::variable<InstanceState> _instanceState
		= InstanceState::Disconnected;
	bool _instanceTransitioning = false;
	InstanceMode _instanceMode = InstanceMode::None;
	base::flat_set<uint32> _unresolvedSsrcs;
	std::vector<tgcalls::GroupParticipantDescription> _preparedParticipants;
	bool _addPreparedParticipantsScheduled = false;
	bool _recordingStoppedByMe = false;

	MTP::DcId _broadcastDcId = 0;
	base::flat_map<not_null<LoadPartTask*>, LoadingPart> _broadcastParts;

	not_null<PeerData*> _joinAs;
	std::vector<not_null<PeerData*>> _possibleJoinAs;
	QString _joinHash;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	bool _initialMuteStateSent = false;
	bool _acceptFields = false;

	rpl::event_stream<Group::ParticipantState> _otherParticipantStateValue;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint32 _mySsrc = 0;
	base::flat_set<uint32> _mySsrcs;
	mtpRequestId _createRequestId = 0;
	mtpRequestId _updateMuteRequestId = 0;

	std::unique_ptr<tgcalls::GroupInstanceCustomImpl> _instance;
	rpl::event_stream<LevelUpdate> _levelUpdates;
	base::flat_map<uint32, Data::LastSpokeTimes> _lastSpoke;
	rpl::event_stream<Group::RejoinEvent> _rejoinEvents;
	rpl::event_stream<> _allowedToSpeakNotifications;
	rpl::event_stream<> _titleChanged;
	base::Timer _lastSpokeCheckTimer;
	base::Timer _checkJoinedTimer;

	crl::time _lastSendProgressUpdate = 0;

	std::shared_ptr<GlobalShortcutManager> _shortcutManager;
	std::shared_ptr<GlobalShortcutValue> _pushToTalk;
	base::Timer _pushToTalkCancelTimer;
	base::Timer _connectingSoundTimer;
	bool _hadJoinedState = false;

	std::unique_ptr<Webrtc::MediaDevices> _mediaDevices;
	QString _audioInputId;
	QString _audioOutputId;

	rpl::lifetime _lifetime;

};

} // namespace Calls
