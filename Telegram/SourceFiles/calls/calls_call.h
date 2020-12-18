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
} // namespace Webrtc

namespace Calls {

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

class Call : public base::has_weak_ptr {
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

		virtual auto getVideoCapture()
			-> std::shared_ptr<tgcalls::VideoCaptureInterface> = 0;

		virtual ~Delegate() = default;

	};

	static constexpr auto kSoundSampleMs = 100;

	enum class Type {
		Incoming,
		Outgoing,
	};
	Call(not_null<Delegate*> delegate, not_null<UserData*> user, Type type, bool video);

	[[nodiscard]] Type type() const {
		return _type;
	}
	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
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
		Ended,
		EndedByOtherDevice,
		ExchangingKeys,
		Waiting,
		Requesting,
		WaitingIncoming,
		Ringing,
		Busy,
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

	void switchVideoOutgoing();
	void answer();
	void hangup();
	void redial();

	bool isKeyShaForFingerprintReady() const;
	bytes::vector getKeyShaForFingerprint() const;

	QString getDebugLog() const;

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	void setCurrentVideoDevice(const QString &deviceId);
	//void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

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

	void handleRequestError(const RPCError &error);
	void handleControllerError(const QString &error);
	void finish(
		FinishType type,
		const MTPPhoneCallDiscardReason &reason
			= MTP_phoneCallDiscardReasonDisconnect());
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
	void confirmAcceptedCall(const MTPDphoneCallAccepted &call);
	void startConfirmedCall(const MTPDphoneCall &call);
	void setState(State state);
	void setStateQueued(State state);
	void setFailedQueued(const QString &error);
	void setSignalBarCount(int count);
	void destroyController();

	void setupOutgoingVideo();
	void updateRemoteMediaState(
		tgcalls::AudioState audio,
		tgcalls::VideoState video);

	const not_null<Delegate*> _delegate;
	const not_null<UserData*> _user;
	MTP::Sender _api;
	Type _type = Type::Outgoing;
	rpl::variable<State> _state = State::Starting;
	rpl::variable<RemoteAudioState> _remoteAudioState = RemoteAudioState::Active;
	rpl::variable<Webrtc::VideoState> _remoteVideoState;
	rpl::event_stream<Error> _errors;
	FinishType _finishAfterRequestingCall = FinishType::None;
	bool _answerAfterDhConfigReceived = false;
	rpl::variable<int> _signalBarCount = kSignalBarStarting;
	crl::time _startTime = 0;
	base::DelayedCallTimer _finishByTimeoutTimer;
	base::Timer _discardByTimeoutTimer;

	rpl::variable<bool> _muted = false;

	DhConfig _dhConfig;
	bytes::vector _ga;
	bytes::vector _gb;
	bytes::vector _gaHash;
	bytes::vector _randomPower;
	MTP::AuthKey::Data _authKey;
	MTPPhoneCallProtocol _protocol;

	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint64 _keyFingerprint = 0;

	std::unique_ptr<tgcalls::Instance> _instance;
	std::shared_ptr<tgcalls::VideoCaptureInterface> _videoCapture;
	const std::unique_ptr<Webrtc::VideoTrack> _videoIncoming;
	const std::unique_ptr<Webrtc::VideoTrack> _videoOutgoing;

	std::unique_ptr<Media::Audio::Track> _waitingTrack;

	rpl::lifetime _lifetime;

};

void UpdateConfig(const std::string &data);

} // namespace Calls
