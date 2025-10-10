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
#include "webrtc/webrtc_device_common.h"
#include "webrtc/webrtc_device_resolver.h"

class History;

namespace tgcalls {
class GroupInstanceCustomImpl;
struct GroupLevelsUpdate;
struct GroupNetworkState;
struct GroupParticipantDescription;
class VideoCaptureInterface;
enum class VideoCodecName;
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

namespace TdE2E {
class Call;
class EncryptDecrypt;
} // namespace TdE2E

namespace Calls {

namespace Group {
struct MuteRequest;
struct VolumeRequest;
struct ParticipantState;
struct JoinInfo;
struct ConferenceInfo;
struct RejoinEvent;
struct RtmpInfo;
enum class VideoQuality;
enum class Error;
class Messages;
} // namespace Group

struct InviteRequest;
struct InviteResult;
struct StartConferenceInfo;

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

	[[nodiscard]] bool rtmp() const noexcept;
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

class GroupCall final
	: public base::has_weak_ptr
	, private Webrtc::CaptureMuteTracker {
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
			RecordingStarted,
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
	GroupCall(not_null<Delegate*> delegate, StartConferenceInfo info);
	~GroupCall();

	[[nodiscard]] CallId id() const {
		return _id;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] not_null<PeerData*> joinAs() const {
		return _joinAs.current();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> joinAsValue() const {
		return _joinAs.value();
	}
	[[nodiscard]] not_null<Group::Messages*> messages() const {
		return _messages.get();
	}
	[[nodiscard]] bool showChooseJoinAs() const;
	[[nodiscard]] TimeId scheduleDate() const {
		return _scheduleDate;
	}
	[[nodiscard]] bool scheduleStartSubscribed() const;
	[[nodiscard]] bool rtmp() const;
	[[nodiscard]] bool conference() const;
	[[nodiscard]] bool listenersHidden() const;
	[[nodiscard]] bool emptyRtmp() const;
	[[nodiscard]] rpl::producer<bool> emptyRtmpValue() const;
	[[nodiscard]] int rtmpVolume() const;

	[[nodiscard]] Group::RtmpInfo rtmpInfo() const;

	void setRtmpInfo(const Group::RtmpInfo &value);

	[[nodiscard]] Data::GroupCall *lookupReal() const;
	[[nodiscard]] std::shared_ptr<Data::GroupCall> conferenceCall() const;
	[[nodiscard]] rpl::producer<not_null<Data::GroupCall*>> real() const;
	[[nodiscard]] rpl::producer<QByteArray> emojiHashValue() const;

	void applyInputCall(const MTPInputGroupCall &inputCall);
	void startConference();
	void start(TimeId scheduleDate, bool rtmp);
	void hangup();
	void discard();
	void rejoinAs(Group::JoinInfo info);
	void rejoinWithHash(const QString &hash);
	void initialJoin();
	void initialJoinRequested();
	void handleUpdate(const MTPUpdate &update);
	void handlePossibleCreateOrJoinResponse(const MTPDupdateGroupCall &data);
	void handlePossibleCreateOrJoinResponse(
		const MTPDupdateGroupCallConnection &data);
	void handleIncomingMessage(const MTPDupdateGroupCallMessage &data);
	void handleIncomingMessage(
		const MTPDupdateGroupCallEncryptedMessage &data);
	void changeTitle(const QString &title);
	void toggleRecording(
		bool enabled,
		const QString &title,
		bool video,
		bool videoPortrait);
	void playSoundRecordingStarted() const;
	[[nodiscard]] bool recordingStoppedByMe() const {
		return _recordingStoppedByMe;
	}
	void startScheduledNow();
	void toggleScheduleStartSubscribed(bool subscribed);
	void setNoiseSuppression(bool enabled);
	void removeConferenceParticipants(
		const base::flat_set<UserId> userIds,
		bool removingStale = false);

	bool emitShareScreenError();
	bool emitShareCameraError();

	void joinDone(
		int64 serverTimeMs,
		const MTPUpdates &result,
		MuteState wasMuteState,
		bool wasVideoStopped,
		bool justCreated = false);
	void joinFail(const QString &error);

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
	[[nodiscard]] bool messagesEnabled() const {
		return _messagesEnabled.current();
	}
	[[nodiscard]] rpl::producer<bool> messagesEnabledValue() const {
		return _messagesEnabled.value();
	}

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

	void inviteUsers(
		const std::vector<InviteRequest> &requests,
		Fn<void(InviteResult)> done);

	std::shared_ptr<GlobalShortcutManager> ensureGlobalShortcutManager();
	void applyGlobalShortcutChanges();

	void pushToTalk(bool pressed, crl::time delay);
	void setNotRequireARGB32();

	[[nodiscard]] std::function<std::vector<uint8_t>(
		std::vector<uint8_t> const &,
		int64_t, bool,
		int32_t)> e2eEncryptDecrypt() const;
	void sendMessage(TextWithTags message);
	[[nodiscard]] MTPInputGroupCall inputCall() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	class LoadPartTask;
	class MediaChannelDescriptionsTask;
	class RequestCurrentTimeTask;
	using GlobalShortcutValue = base::GlobalShortcutValue;
	using Error = Group::Error;
	struct SinkPointer;

	static constexpr uint32 kDisabledSsrc = uint32(-1);
	static constexpr int kSubChainsCount = 2;

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
	struct JoinPayload {
		uint32 ssrc = 0;
		QByteArray json;
	};
	struct JoinState {
		uint32 ssrc = 0;
		JoinAction action = JoinAction::None;
		JoinPayload payload;
		bool nextActionPending = false;

		void finish(uint32 updatedSsrc = 0) {
			action = JoinAction::None;
			ssrc = updatedSsrc;
		}
	};
	struct SubChainPending {
		QVector<MTPbytes> blocks;
		int next = 0;
	};
	struct SubChainState {
		std::vector<SubChainPending> pending;
		mtpRequestId requestId = 0;
		bool inShortPoll = false;
	};

	friend inline constexpr bool is_flag_type(SendUpdateType) {
		return true;
	}

	GroupCall(
		not_null<Delegate*> delegate,
		Group::JoinInfo join,
		StartConferenceInfo conference,
		const MTPInputGroupCall &inputCall);

	void broadcastPartStart(std::shared_ptr<LoadPartTask> task);
	void broadcastPartCancel(not_null<LoadPartTask*> task);
	void mediaChannelDescriptionsStart(
		std::shared_ptr<MediaChannelDescriptionsTask> task);
	void mediaChannelDescriptionsCancel(
		not_null<MediaChannelDescriptionsTask*> task);
	void requestCurrentTimeStart(
		std::shared_ptr<RequestCurrentTimeTask> task);
	void requestCurrentTimeCancel(
		not_null<RequestCurrentTimeTask*> task);
	[[nodiscard]] int64 approximateServerTimeInMs() const;

	[[nodiscard]] bool mediaChannelDescriptionsFill(
		not_null<MediaChannelDescriptionsTask*> task,
		Fn<bool(uint32)> resolved = nullptr);
	void checkMediaChannelDescriptions(Fn<bool(uint32)> resolved = nullptr);

	void handlePossibleCreateOrJoinResponse(const MTPDgroupCall &data);
	void handlePossibleDiscarded(const MTPDgroupCallDiscarded &data);
	void handleUpdate(const MTPDupdateGroupCall &data);
	void handleUpdate(const MTPDupdateGroupCallParticipants &data);
	void handleUpdate(const MTPDupdateGroupCallChainBlocks &data);
	void applySubChainUpdate(
		int subchain,
		const QVector<MTPbytes> &blocks,
		int next);
	[[nodiscard]] auto lookupVideoCodecPreferences() const
		-> std::vector<tgcalls::VideoCodecName>;
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
	void startRejoin();
	void rejoin();
	void leave();
	void rejoin(not_null<PeerData*> as);
	void setJoinAs(not_null<PeerData*> as);
	void saveDefaultJoinAs(not_null<PeerData*> as);
	void subscribeToReal(not_null<Data::GroupCall*> real);
	void setScheduledDate(TimeId date);
	void setMessagesEnabled(bool enabled);
	void rejoinPresentation();
	void leavePresentation();
	void checkNextJoinAction();
	void sendJoinRequest();
	void refreshLastBlockAndJoin();
	void requestSubchainBlocks(int subchain, int height);
	void sendOutboundBlock(QByteArray block);

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

	void captureMuteChanged(bool mute) override;
	rpl::producer<Webrtc::DeviceResolvedId> captureMuteDeviceId() override;

	void setupMediaDevices();
	void setupOutgoingVideo();
	void initConferenceE2E();
	void setupConferenceCall();
	void trackParticipantsWithAccess();
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

	void processConferenceStart(StartConferenceInfo conference);
	void inviteToConference(
		InviteRequest request,
		Fn<not_null<InviteResult*>()> resultAddress,
		Fn<void()> finishRequest);

	[[nodiscard]] int activeVideoSendersCount() const;

	[[nodiscard]] MTPInputGroupCall inputCallSafe() const;

	const not_null<Delegate*> _delegate;
	std::shared_ptr<Data::GroupCall> _conferenceCall;
	std::unique_ptr<TdE2E::Call> _e2e;
	std::shared_ptr<TdE2E::EncryptDecrypt> _e2eEncryptDecrypt;
	rpl::variable<QByteArray> _emojiHash;
	QByteArray _pendingOutboundBlock;
	std::shared_ptr<StartConferenceInfo> _startConferenceInfo;

	not_null<PeerData*> _peer; // Can change in legacy group migration.
	rpl::event_stream<PeerData*> _peerStream;
	not_null<History*> _history; // Can change in legacy group migration.
	MTP::Sender _api;
	rpl::event_stream<not_null<Data::GroupCall*>> _realChanges;
	rpl::variable<State> _state = State::Creating;
	base::flat_set<uint32> _unresolvedSsrcs;
	rpl::event_stream<Error> _errors;
	std::vector<Fn<void()>> _rejoinedCallbacks;
	std::unique_ptr<Group::Messages> _messages;
	bool _recordingStoppedByMe = false;
	bool _requestedVideoChannelsUpdateScheduled = false;

	MTP::DcId _broadcastDcId = 0;
	base::flat_map<not_null<LoadPartTask*>, LoadingPart> _broadcastParts;
	base::flat_set<
		std::shared_ptr<MediaChannelDescriptionsTask>,
		base::pointer_comparator<
			MediaChannelDescriptionsTask>> _mediaChannelDescriptionses;
	base::flat_set<
		std::shared_ptr<RequestCurrentTimeTask>,
		base::pointer_comparator<
			RequestCurrentTimeTask>> _requestCurrentTimes;
	mtpRequestId _requestCurrentTimeRequestId = 0;

	rpl::variable<not_null<PeerData*>> _joinAs;
	std::vector<not_null<PeerData*>> _possibleJoinAs;
	QString _joinHash;
	QString _conferenceLinkSlug;
	MsgId _conferenceJoinMessageId;
	int64 _serverTimeMs = 0;
	crl::time _serverTimeMsGotAt = 0;

	QString _rtmpUrl;
	QString _rtmpKey;

	rpl::variable<MuteState> _muted = MuteState::Muted;
	rpl::variable<bool> _canManage = false;
	rpl::variable<bool> _videoIsWorking = false;
	rpl::variable<bool> _emptyRtmp = false;
	rpl::variable<bool> _messagesEnabled = false;
	bool _initialMuteStateSent = false;
	bool _acceptFields = false;

	rpl::event_stream<Group::ParticipantState> _otherParticipantStateValue;
	std::vector<MTPGroupCallParticipant> _queuedSelfUpdates;

	CallId _id = 0;
	CallId _accessHash = 0;
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

	Fn<void(Webrtc::DeviceResolvedId)> _setDeviceIdCallback;
	Webrtc::DeviceResolver _playbackDeviceId;
	Webrtc::DeviceResolver _captureDeviceId;
	Webrtc::DeviceResolver _cameraDeviceId;

	std::shared_ptr<GlobalShortcutManager> _shortcutManager;
	std::shared_ptr<GlobalShortcutValue> _pushToTalk;
	base::Timer _pushToTalkCancelTimer;
	base::Timer _connectingSoundTimer;
	bool _hadJoinedState = false;
	bool _listenersHidden = false;
	bool _rtmp = false;
	bool _reloadedStaleCall = false;
	int _rtmpVolume = 0;

	SubChainState _subchains[kSubChainsCount];

	rpl::lifetime _lifetime;

};

[[nodiscard]] TextWithEntities ComposeInviteResultToast(
	const InviteResult &result);

} // namespace Calls
