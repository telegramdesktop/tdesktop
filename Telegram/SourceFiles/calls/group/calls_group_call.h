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
class VideoCaptureInterface;
} // namespace tgcalls

namespace base {
class GlobalShortcutManager;
class GlobalShortcutValue;
} // namespace base

namespace Webrtc {
class MediaDevices;
class VideoTrack;
enum class VideoState;
} // namespace Webrtc

namespace Data {
struct LastSpokeTimes;
struct GroupCallParticipant;
class GroupCall;
} // namespace Data

namespace Calls {

namespace Group {
struct MuteRequest;
struct VolumeRequest;
struct ParticipantState;
struct JoinInfo;
struct RejoinEvent;
enum class VideoQuality;
enum class Error;
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

enum class VideoEndpointType {
	Camera,
	Screen,
};

struct VideoEndpoint {
	VideoEndpoint() = default;
	VideoEndpoint(
		VideoEndpointType type,
		not_null<PeerData*> peer,
		std::string id)
	: type(type)
	, peer(peer)
	, id(std::move(id)) {
	}

	VideoEndpointType type = VideoEndpointType::Camera;
	PeerData *peer = nullptr;
	std::string id;

	[[nodiscard]] bool empty() const noexcept {
		Expects(id.empty() || peer != nullptr);

		return id.empty();
	}
	[[nodiscard]] explicit operator bool() const noexcept {
		return !empty();
	}
};

inline bool operator==(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return (a.id == b.id);
}

inline bool operator!=(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return !(a == b);
}

inline bool operator<(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return (a.peer < b.peer)
		|| (a.peer == b.peer && a.id < b.id);
}

inline bool operator>(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return (b < a);
}

inline bool operator<=(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return !(b < a);
}

inline bool operator>=(
		const VideoEndpoint &a,
		const VideoEndpoint &b) noexcept {
	return !(a < b);
}

struct VideoStateToggle {
	VideoEndpoint endpoint;
	bool value = false;
};

struct VideoQualityRequest {
	VideoEndpoint endpoint;
	Group::VideoQuality quality = Group::VideoQuality();
};

struct ParticipantVideoParams;

[[nodiscard]] std::shared_ptr<ParticipantVideoParams> ParseVideoParams(
	const tl::conditional<MTPGroupCallParticipantVideo> &camera,
	const tl::conditional<MTPGroupCallParticipantVideo> &screen,
	const std::shared_ptr<ParticipantVideoParams> &existing);

[[nodiscard]] const std::string &GetCameraEndpoint(
	const std::shared_ptr<ParticipantVideoParams> &params);
[[nodiscard]] const std::string &GetScreenEndpoint(
	const std::shared_ptr<ParticipantVideoParams> &params);
[[nodiscard]] bool IsCameraPaused(
	const std::shared_ptr<ParticipantVideoParams> &params);
[[nodiscard]] bool IsScreenPaused(
	const std::shared_ptr<ParticipantVideoParams> &params);
[[nodiscard]] uint32 GetAdditionalAudioSsrc(
	const std::shared_ptr<ParticipantVideoParams> &params);

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
		virtual auto groupCallGetVideoCapture(const QString &deviceId)
			-> std::shared_ptr<tgcalls::VideoCaptureInterface> = 0;

		[[nodiscard]] virtual FnMut<void()> groupCallAddAsyncWaiter() = 0;
	};

	using GlobalShortcutManager = base::GlobalShortcutManager;

	struct VideoTrack;

	[[nodiscard]] static not_null<PeerData*> TrackPeer(
		const std::unique_ptr<VideoTrack> &track);
	[[nodiscard]] static not_null<Webrtc::VideoTrack*> TrackPointer(
		const std::unique_ptr<VideoTrack> &track);
	[[nodiscard]] static rpl::producer<QSize> TrackSizeValue(
		const std::unique_ptr<VideoTrack> &track);

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
	[[nodiscard]] TimeId scheduleDate() const {
		return _scheduleDate;
	}
	[[nodiscard]] bool scheduleStartSubscribed() const;

	[[nodiscard]] Data::GroupCall *lookupReal() const;
	[[nodiscard]] rpl::producer<not_null<Data::GroupCall*>> real() const;

	void start(TimeId scheduleDate);
	void hangup();
	void discard();
	void rejoinAs(Group::JoinInfo info);
	void rejoinWithHash(const QString &hash);
	void join(const MTPInputGroupCall &inputCall);
	void handleUpdate(const MTPUpdate &update);
	void handlePossibleCreateOrJoinResponse(const MTPDupdateGroupCall &data);
	void handlePossibleCreateOrJoinResponse(
		const MTPDupdateGroupCallConnection &data);
	void changeTitle(const QString &title);
	void toggleRecording(bool enabled, const QString &title);
	[[nodiscard]] bool recordingStoppedByMe() const {
		return _recordingStoppedByMe;
	}
	void startScheduledNow();
	void toggleScheduleStartSubscribed(bool subscribed);
	void setNoiseSuppression(bool enabled);

	bool emitShareScreenError();
	bool emitShareCameraError();

	[[nodiscard]] rpl::producer<Group::Error> errors() const {
		return _errors.events();
	}

	void addVideoOutput(
		const std::string &endpoint,
		not_null<Webrtc::VideoTrack*> track);

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
		Waiting,
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
	[[nodiscard]] auto videoStreamActiveUpdates() const
	-> rpl::producer<VideoStateToggle> {
		return _videoStreamActiveUpdates.events();
	}
	[[nodiscard]] auto videoStreamShownUpdates() const
	-> rpl::producer<VideoStateToggle> {
		return _videoStreamShownUpdates.events();
	}
	void requestVideoQuality(
		const VideoEndpoint &endpoint,
		Group::VideoQuality quality);

	[[nodiscard]] bool videoEndpointPinned() const {
		return _videoEndpointPinned.current();
	}
	[[nodiscard]] rpl::producer<bool> videoEndpointPinnedValue() const {
		return _videoEndpointPinned.value();
	}
	void pinVideoEndpoint(VideoEndpoint endpoint);

	void showVideoEndpointLarge(VideoEndpoint endpoint);
	[[nodiscard]] const VideoEndpoint &videoEndpointLarge() const {
		return _videoEndpointLarge.current();
	}
	[[nodiscard]] auto videoEndpointLargeValue() const
	-> rpl::producer<VideoEndpoint> {
		return _videoEndpointLarge.value();
	}
	[[nodiscard]] auto activeVideoTracks() const
	-> const base::flat_map<VideoEndpoint, std::unique_ptr<VideoTrack>> & {
		return _activeVideoTracks;
	}
	[[nodiscard]] auto shownVideoTracks() const
	-> const base::flat_set<VideoEndpoint> & {
		return _shownVideoTracks;
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

	[[nodiscard]] bool mutedByAdmin() const;
	[[nodiscard]] bool canManage() const;
	[[nodiscard]] rpl::producer<bool> canManageValue() const;
	[[nodiscard]] bool videoIsWorking() const {
		return _videoIsWorking.current();
	}
	[[nodiscard]] rpl::producer<bool> videoIsWorkingValue() const {
		return _videoIsWorking.value();
	}
	[[nodiscard]] bool hasNotShownVideo() const {
		return _hasNotShownVideo.current();
	}
	[[nodiscard]] rpl::producer<bool> hasNotShownVideoValue() const {
		return _hasNotShownVideo.value();
	}

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	void setCurrentVideoDevice(const QString &deviceId);
	[[nodiscard]] bool isSharingScreen() const;
	[[nodiscard]] rpl::producer<bool> isSharingScreenValue() const;
	[[nodiscard]] bool isScreenPaused() const;
	[[nodiscard]] const std::string &screenSharingEndpoint() const;
	[[nodiscard]] bool isSharingCamera() const;
	[[nodiscard]] rpl::producer<bool> isSharingCameraValue() const;
	[[nodiscard]] bool isCameraPaused() const;
	[[nodiscard]] const std::string &cameraSharingEndpoint() const;
	[[nodiscard]] QString screenSharingDeviceId() const;
	[[nodiscard]] bool screenSharingWithAudio() const;
	void toggleVideo(bool active);
	void toggleScreenSharing(
		std::optional<QString> uniqueId,
		bool withAudio = false);
	[[nodiscard]] bool hasVideoWithFrames() const;
	[[nodiscard]] rpl::producer<bool> hasVideoWithFramesValue() const;

	void toggleMute(const Group::MuteRequest &data);
	void changeVolume(const Group::VolumeRequest &data);
	std::variant<int, not_null<UserData*>> inviteUsers(
		const std::vector<not_null<UserData*>> &users);

	std::shared_ptr<GlobalShortcutManager> ensureGlobalShortcutManager();
	void applyGlobalShortcutChanges();

	void pushToTalk(bool pressed, crl::time delay);
	void setNotRequireARGB32();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class LoadPartTask;
	class MediaChannelDescriptionsTask;

public:
	void broadcastPartStart(std::shared_ptr<LoadPartTask> task);
	void broadcastPartCancel(not_null<LoadPartTask*> task);
	void mediaChannelDescriptionsStart(
		std::shared_ptr<MediaChannelDescriptionsTask> task);
	void mediaChannelDescriptionsCancel(
		not_null<MediaChannelDescriptionsTask*> task);

private:
	using GlobalShortcutValue = base::GlobalShortcutValue;
	using Error = Group::Error;
	struct SinkPointer;

	static constexpr uint32 kDisabledSsrc = uint32(-1);

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
		Mute          = 0x01,
		RaiseHand     = 0x02,
		CameraStopped = 0x04,
		CameraPaused  = 0x08,
		ScreenPaused  = 0x10,
	};
	enum class JoinAction {
		None,
		Joining,
		Leaving,
	};
	struct JoinState {
		uint32 ssrc = 0;
		JoinAction action = JoinAction::None;
		bool nextActionPending = false;

		void finish(uint32 updatedSsrc = 0) {
			action = JoinAction::None;
			ssrc = updatedSsrc;
		}
	};

	friend inline constexpr bool is_flag_type(SendUpdateType) {
		return true;
	}

	[[nodiscard]] bool mediaChannelDescriptionsFill(
		not_null<MediaChannelDescriptionsTask*> task,
		Fn<bool(uint32)> resolved = nullptr);
	void checkMediaChannelDescriptions(Fn<bool(uint32)> resolved = nullptr);

	void handlePossibleCreateOrJoinResponse(const MTPDgroupCall &data);
	void handlePossibleDiscarded(const MTPDgroupCallDiscarded &data);
	void handleUpdate(const MTPDupdateGroupCall &data);
	void handleUpdate(const MTPDupdateGroupCallParticipants &data);
	bool tryCreateController();
	void destroyController();
	bool tryCreateScreencast();
	void destroyScreencast();

	void emitShareCameraError(Error error);
	void emitShareScreenError(Error error);

	void setState(State state);
	void finish(FinishType type);
	void maybeSendMutedUpdate(MuteState previous);
	void sendSelfUpdate(SendUpdateType type);
	void updateInstanceMuteState();
	void updateInstanceVolumes();
	void updateInstanceVolume(
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant &now);
	void applyMeInCallLocally();
	void rejoin();
	void leave();
	void rejoin(not_null<PeerData*> as);
	void setJoinAs(not_null<PeerData*> as);
	void saveDefaultJoinAs(not_null<PeerData*> as);
	void subscribeToReal(not_null<Data::GroupCall*> real);
	void setScheduledDate(TimeId date);
	void rejoinPresentation();
	void leavePresentation();
	void checkNextJoinAction();

	void audioLevelsUpdated(const tgcalls::GroupLevelsUpdate &data);
	void setInstanceConnected(tgcalls::GroupNetworkState networkState);
	void setInstanceMode(InstanceMode mode);
	void setScreenInstanceConnected(tgcalls::GroupNetworkState networkState);
	void setScreenInstanceMode(InstanceMode mode);
	void checkLastSpoke();
	void pushToTalkCancel();

	void checkGlobalShortcutAvailability();
	void checkJoined();
	void checkFirstTimeJoined();
	void notifyAboutAllowedToSpeak();

	void playConnectingSound();
	void stopConnectingSound();
	void playConnectingSoundOnce();

	void updateRequestedVideoChannels();
	void updateRequestedVideoChannelsDelayed();
	void fillActiveVideoEndpoints();
	void refreshHasNotShownVideo();

	void editParticipant(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);
	void applyParticipantLocally(
		not_null<PeerData*> participantPeer,
		bool mute,
		std::optional<int> volume);
	void applyQueuedSelfUpdates();
	void sendPendingSelfUpdates();
	void applySelfUpdate(const MTPDgroupCallParticipant &data);
	void applyOtherParticipantUpdate(const MTPDgroupCallParticipant &data);

	void setupMediaDevices();
	void setupOutgoingVideo();
	void setScreenEndpoint(std::string endpoint);
	void setCameraEndpoint(std::string endpoint);
	void addVideoOutput(const std::string &endpoint, SinkPointer sink);
	void setVideoEndpointLarge(VideoEndpoint endpoint);

	void markEndpointActive(
		VideoEndpoint endpoint,
		bool active,
		bool paused);
	void markTrackPaused(const VideoEndpoint &endpoint, bool paused);
	void markTrackShown(const VideoEndpoint &endpoint, bool shown);

	[[nodiscard]] int activeVideoSendersCount() const;

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	not_null<PeerData*> _peer; // Can change in legacy group migration.
	rpl::event_stream<PeerData*> _peerStream;
	not_null<History*> _history; // Can change in legacy group migration.
	MTP::Sender _api;
	rpl::event_stream<not_null<Data::GroupCall*>> _realChanges;
	rpl::variable<State> _state = State::Creating;
	base::flat_set<uint32> _unresolvedSsrcs;
	rpl::event_stream<Error> _errors;
	bool _recordingStoppedByMe = false;
	bool _requestedVideoChannelsUpdateScheduled = false;

	MTP::DcId _broadcastDcId = 0;
	base::flat_map<not_null<LoadPartTask*>, LoadingPart> _broadcastParts;
	base::flat_set<
		std::shared_ptr<
			MediaChannelDescriptionsTask>,
		base::pointer_comparator<MediaChannelDescriptionsTask>> _mediaChannelDescriptionses;

	not_null<PeerData*> _joinAs;
	std::vector<not_null<PeerData*>> _possibleJoinAs;
	QString _joinHash;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	rpl::variable<bool> _canManage = false;
	rpl::variable<bool> _videoIsWorking = false;
	rpl::variable<bool> _hasNotShownVideo = false;
	bool _initialMuteStateSent = false;
	bool _acceptFields = false;

	rpl::event_stream<Group::ParticipantState> _otherParticipantStateValue;
	std::vector<MTPGroupCallParticipant> _queuedSelfUpdates;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	JoinState _joinState;
	JoinState _screenJoinState;
	std::string _cameraEndpoint;
	std::string _screenEndpoint;
	TimeId _scheduleDate = 0;
	base::flat_set<uint32> _mySsrcs;
	mtpRequestId _createRequestId = 0;
	mtpRequestId _selfUpdateRequestId = 0;

	rpl::variable<InstanceState> _instanceState
		= InstanceState::Disconnected;
	bool _instanceTransitioning = false;
	InstanceMode _instanceMode = InstanceMode::None;
	std::unique_ptr<tgcalls::GroupInstanceCustomImpl> _instance;
	base::has_weak_ptr _instanceGuard;
	std::shared_ptr<tgcalls::VideoCaptureInterface> _cameraCapture;
	rpl::variable<Webrtc::VideoState> _cameraState;
	rpl::variable<bool> _isSharingCamera = false;
	base::flat_map<std::string, SinkPointer> _pendingVideoOutputs;

	rpl::variable<InstanceState> _screenInstanceState
		= InstanceState::Disconnected;
	InstanceMode _screenInstanceMode = InstanceMode::None;
	std::unique_ptr<tgcalls::GroupInstanceCustomImpl> _screenInstance;
	base::has_weak_ptr _screenInstanceGuard;
	std::shared_ptr<tgcalls::VideoCaptureInterface> _screenCapture;
	rpl::variable<Webrtc::VideoState> _screenState;
	rpl::variable<bool> _isSharingScreen = false;
	QString _screenDeviceId;
	bool _screenWithAudio = false;

	base::flags<SendUpdateType> _pendingSelfUpdates;
	bool _requireARGB32 = true;

	rpl::event_stream<LevelUpdate> _levelUpdates;
	rpl::event_stream<VideoStateToggle> _videoStreamActiveUpdates;
	rpl::event_stream<VideoStateToggle> _videoStreamPausedUpdates;
	rpl::event_stream<VideoStateToggle> _videoStreamShownUpdates;
	base::flat_map<
		VideoEndpoint,
		std::unique_ptr<VideoTrack>> _activeVideoTracks;
	base::flat_set<VideoEndpoint> _shownVideoTracks;
	rpl::variable<VideoEndpoint> _videoEndpointLarge;
	rpl::variable<bool> _videoEndpointPinned = false;
	crl::time _videoLargeTillTime = 0;
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
	QString _cameraInputId;

	rpl::lifetime _lifetime;

};

} // namespace Calls
