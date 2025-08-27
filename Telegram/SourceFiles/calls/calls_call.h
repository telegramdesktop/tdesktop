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
#include "webrtc/webrtc_device_resolver.h"

namespace Data {
class GroupCall;
} // namespace Data

namespace Media {
namespace Audio {
class Track;
} // namespace Audio
} // namespace Media

namespace tgcalls {
class Instance;
class VideoCaptureInterface;
enum class State;
enum class VideoState;
enum class AudioState;
} // namespace tgcalls

namespace Webrtc {
enum class VideoState;
class VideoTrack;
struct DeviceResolvedId;
} // namespace Webrtc

namespace Calls {

struct StartConferenceInfo;

struct DhConfig {
	int32 version = 0;
	int32 g = 0;
	bytes::vector p;
};

enum class ErrorType {
	NoCamera,
	NoMicrophone,
	NotStartedCall,
	NotVideoCall,
	Unknown,
};

struct Error {
	ErrorType type = ErrorType::Unknown;
	QString details;
};

enum class CallType {
	Incoming,
	Outgoing,
};

class Call final
	: public base::has_weak_ptr
	, private Webrtc::CaptureMuteTracker {
public:
	class Delegate {
	public:
		virtual DhConfig getDhConfig() const = 0;
		virtual void callFinished(not_null<Call*> call) = 0;
		virtual void callFailed(not_null<Call*> call) = 0;
		virtual void callRedial(not_null<Call*> call) = 0;

		enum class CallSound {
			Connecting,
			Busy,
			Ended,
		};
		virtual void callPlaySound(CallSound sound) = 0;
		virtual void callRequestPermissionsOrFail(
			Fn<void()> onSuccess,
			bool video) = 0;

		virtual auto callGetVideoCapture(
			const QString &deviceId,
			bool isScreenCapture)
		-> std::shared_ptr<tgcalls::VideoCaptureInterface> = 0;

		virtual ~Delegate() = default;

	};

	static constexpr auto kSoundSampleMs = 100;

	using Type = CallType;
	Call(
		not_null<Delegate*> delegate,
		not_null<UserData*> user,
		Type type,
		bool video);
	Call(
		not_null<Delegate*> delegate,
		not_null<UserData*> user,
		CallId conferenceId,
		MsgId conferenceInviteMsgId,
		std::vector<not_null<PeerData*>> conferenceParticipants,
		bool video);

	[[nodiscard]] Type type() const {
		return _type;
	}
	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}
	[[nodiscard]] CallId id() const {
		return _id;
	}
	[[nodiscard]] bool conferenceInvite() const {
		return _conferenceId != 0;
	}
	[[nodiscard]] CallId conferenceId() const {
		return _conferenceId;
	}
	[[nodiscard]] MsgId conferenceInviteMsgId() const {
		return _conferenceInviteMsgId;
	}
	[[nodiscard]] auto conferenceParticipants() const
	-> const std::vector<not_null<PeerData*>> & {
		return _conferenceParticipants;
	}
	[[nodiscard]] bool isIncomingWaiting() const;

	void start(bytes::const_span random);
	bool handleUpdate(const MTPPhoneCall &call);
	bool handleSignalingData(const MTPDupdatePhoneCallSignalingData &data);

	enum State {
		Starting,
		WaitingInit,
		WaitingInitAck,
		Established,
		FailedHangingUp,
		Failed,
		HangingUp,
		MigrationHangingUp,
		Ended,
		EndedByOtherDevice,
		ExchangingKeys,
		Waiting,
		Requesting,
		WaitingIncoming,
		Ringing,
		Busy,
		WaitingUserConfirmation,
	};
	[[nodiscard]] State state() const {
		return _state.current();
	}
	[[nodiscard]] rpl::producer<State> stateValue() const {
		return _state.value();
	}

	[[nodiscard]] rpl::producer<Error> errors() const {
		return _errors.events();
	}

	[[nodiscard]] rpl::producer<bool> confereceSupportedValue() const {
		return _conferenceSupported.value();
	}

	enum class RemoteAudioState {
		Muted,
		Active,
	};
	[[nodiscard]] RemoteAudioState remoteAudioState() const {
		return _remoteAudioState.current();
	}
	[[nodiscard]] auto remoteAudioStateValue() const
	-> rpl::producer<RemoteAudioState> {
		return _remoteAudioState.value();
	}

	[[nodiscard]] Webrtc::VideoState remoteVideoState() const {
		return _remoteVideoState.current();
	}
	[[nodiscard]] auto remoteVideoStateValue() const
	-> rpl::producer<Webrtc::VideoState> {
		return _remoteVideoState.value();
	}

	enum class RemoteBatteryState {
		Low,
		Normal,
	};
	[[nodiscard]] RemoteBatteryState remoteBatteryState() const {
		return _remoteBatteryState.current();
	}
	[[nodiscard]] auto remoteBatteryStateValue() const
	-> rpl::producer<RemoteBatteryState> {
		return _remoteBatteryState.value();
	}

	static constexpr auto kSignalBarStarting = -1;
	static constexpr auto kSignalBarFinished = -2;
	static constexpr auto kSignalBarCount = 4;
	[[nodiscard]] rpl::producer<int> signalBarCountValue() const {
		return _signalBarCount.value();
	}

	void setMuted(bool mute);
	[[nodiscard]] bool muted() const {
		return _muted.current();
	}
	[[nodiscard]] rpl::producer<bool> mutedValue() const {
		return _muted.value();
	}

	[[nodiscard]] not_null<Webrtc::VideoTrack*> videoIncoming() const;
	[[nodiscard]] not_null<Webrtc::VideoTrack*> videoOutgoing() const;

	crl::time getDurationMs() const;
	float64 getWaitingSoundPeakValue() const;

	void applyUserConfirmation();
	void answer();
	void hangup(
		Data::GroupCall *migrateCall = nullptr,
		const QString &migrateSlug = QString());
	void redial();

	bool isKeyShaForFingerprintReady() const;
	bytes::vector getKeyShaForFingerprint() const;

	QString getDebugLog() const;

	//void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

	[[nodiscard]] QString videoDeviceId() const {
		return _videoCaptureDeviceId;
	}

	[[nodiscard]] bool isSharingVideo() const;
	[[nodiscard]] bool isSharingCamera() const;
	[[nodiscard]] bool isSharingScreen() const;
	[[nodiscard]] QString cameraSharingDeviceId() const;
	[[nodiscard]] QString screenSharingDeviceId() const;
	void toggleCameraSharing(bool enabled);
	void toggleScreenSharing(std::optional<QString> uniqueId);
	[[nodiscard]] auto peekVideoCapture() const
		-> std::shared_ptr<tgcalls::VideoCaptureInterface>;

	[[nodiscard]] auto playbackDeviceIdValue() const
		-> rpl::producer<Webrtc::DeviceResolvedId>;
	[[nodiscard]] auto captureDeviceIdValue() const
		-> rpl::producer<Webrtc::DeviceResolvedId>;
	[[nodiscard]] auto cameraDeviceIdValue() const
		-> rpl::producer<Webrtc::DeviceResolvedId>;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~Call();

private:
	enum class FinishType {
		None,
		Ended,
		Failed,
	};

	void handleRequestError(const QString &error);
	void handleControllerError(const QString &error);
	void finish(
		FinishType type,
		const MTPPhoneCallDiscardReason &reason
			= MTP_phoneCallDiscardReasonDisconnect(),
		Data::GroupCall *migrateCall = nullptr);
	void finishByMigration(const QString &slug);
	void startOutgoing();
	void startIncoming();
	void startWaitingTrack();
	void sendSignalingData(const QByteArray &data);

	void generateModExpFirst(bytes::const_span randomSeed);
	void handleControllerStateChange(tgcalls::State state);
	void handleControllerBarCountChange(int count);
	void createAndStartController(const MTPDphoneCall &call);

	template <typename T>
	bool checkCallCommonFields(const T &call);
	bool checkCallFields(const MTPDphoneCall &call);
	bool checkCallFields(const MTPDphoneCallAccepted &call);

	void actuallyAnswer();
	void acceptConferenceInvite();
	void confirmAcceptedCall(const MTPDphoneCallAccepted &call);
	void startConfirmedCall(const MTPDphoneCall &call);
	void setState(State state);
	void setStateQueued(State state);
	void setFailedQueued(const QString &error);
	void setSignalBarCount(int count);
	void destroyController();

	void captureMuteChanged(bool mute) override;
	rpl::producer<Webrtc::DeviceResolvedId> captureMuteDeviceId() override;

	void setupMediaDevices();
	void setupOutgoingVideo();
	void updateRemoteMediaState(
		tgcalls::AudioState audio,
		tgcalls::VideoState video);

	[[nodiscard]] StartConferenceInfo migrateConferenceInfo(
		StartConferenceInfo extend);

	const not_null<Delegate*> _delegate;
	const not_null<UserData*> _user;
	MTP::Sender _api;
	Type _type = Type::Outgoing;
	rpl::variable<State> _state = State::Starting;
	rpl::variable<bool> _conferenceSupported = false;
	rpl::variable<RemoteAudioState> _remoteAudioState
		= RemoteAudioState::Active;
	rpl::variable<Webrtc::VideoState> _remoteVideoState;
	rpl::variable<RemoteBatteryState> _remoteBatteryState
		= RemoteBatteryState::Normal;
	rpl::event_stream<Error> _errors;
	FinishType _finishAfterRequestingCall = FinishType::None;
	bool _answerAfterDhConfigReceived = false;
	rpl::variable<int> _signalBarCount = kSignalBarStarting;
	crl::time _startTime = 0;
	base::DelayedCallTimer _finishByTimeoutTimer;
	base::Timer _discardByTimeoutTimer;

	Fn<void(Webrtc::DeviceResolvedId)> _setDeviceIdCallback;
	Webrtc::DeviceResolver _playbackDeviceId;
	Webrtc::DeviceResolver _captureDeviceId;
	Webrtc::DeviceResolver _cameraDeviceId;

	rpl::variable<bool> _muted = false;

	DhConfig _dhConfig;
	bytes::vector _ga;
	bytes::vector _gb;
	bytes::vector _gaHash;
	bytes::vector _randomPower;
	MTP::AuthKey::Data _authKey;

	CallId _id = 0;
	uint64 _accessHash = 0;
	uint64 _keyFingerprint = 0;

	CallId _conferenceId = 0;
	MsgId _conferenceInviteMsgId = 0;
	std::vector<not_null<PeerData*>> _conferenceParticipants;

	std::unique_ptr<tgcalls::Instance> _instance;
	std::shared_ptr<tgcalls::VideoCaptureInterface> _videoCapture;
	QString _videoCaptureDeviceId;
	bool _videoCaptureIsScreencast = false;
	const std::unique_ptr<Webrtc::VideoTrack> _videoIncoming;
	const std::unique_ptr<Webrtc::VideoTrack> _videoOutgoing;

	std::unique_ptr<Media::Audio::Track> _waitingTrack;

	rpl::lifetime _instanceLifetime;
	rpl::lifetime _lifetime;

};

void UpdateConfig(const std::string &data);

} // namespace Calls
