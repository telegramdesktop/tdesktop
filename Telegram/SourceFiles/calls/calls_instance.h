/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "calls/calls_call.h"

namespace Platform {
enum class PermissionType;
} // namespace Platform

namespace Media {
namespace Audio {
class Track;
} // namespace Audio
} // namespace Media

namespace Main {
class Session;
} // namespace Main

namespace Calls {

class Panel;

class Instance
	: private Call::Delegate
	, private base::Subscriber
	, public base::has_weak_ptr {
public:
	Instance();
	~Instance();

	void startOutgoingCall(not_null<UserData*> user, bool video);
	void handleUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update);
	void showInfoPanel(not_null<Call*> call);
	[[nodiscard]] Call *currentCall() const;
	[[nodiscard]] rpl::producer<Call*> currentCallValue() const;
	std::shared_ptr<tgcalls::VideoCaptureInterface> getVideoCapture() override;

	[[nodiscard]] bool isQuitPrevent();

private:
	not_null<Call::Delegate*> getCallDelegate() {
		return static_cast<Call::Delegate*>(this);
	}
	DhConfig getDhConfig() const override {
		return _dhConfig;
	}
	void callFinished(not_null<Call*> call) override;
	void callFailed(not_null<Call*> call) override;
	void callRedial(not_null<Call*> call) override;
	using Sound = Call::Delegate::Sound;
	void playSound(Sound sound) override;
	void createCall(not_null<UserData*> user, Call::Type type, bool video);
	void destroyCall(not_null<Call*> call);
	void requestPermissionsOrFail(Fn<void()> onSuccess) override;
	void requestPermissionOrFail(Platform::PermissionType type, Fn<void()> onSuccess);

	void handleSignalingData(const MTPDupdatePhoneCallSignalingData &data);

	void refreshDhConfig();
	void refreshServerConfig(not_null<Main::Session*> session);
	bytes::const_span updateDhConfig(const MTPmessages_DhConfig &data);

	bool alreadyInCall();
	void handleCallUpdate(
		not_null<Main::Session*> session,
		const MTPPhoneCall &call);

	DhConfig _dhConfig;

	crl::time _lastServerConfigUpdateTime = 0;
	base::weak_ptr<Main::Session> _serverConfigRequestSession;
	std::weak_ptr<tgcalls::VideoCaptureInterface> _videoCapture;

	std::unique_ptr<Call> _currentCall;
	rpl::event_stream<Call*> _currentCallChanges;
	std::unique_ptr<Panel> _currentCallPanel;
	base::Observable<Call*> _currentCallChanged;
	base::Observable<FullMsgId> _newServiceMessage;

	std::unique_ptr<Media::Audio::Track> _callConnectingTrack;
	std::unique_ptr<Media::Audio::Track> _callEndedTrack;
	std::unique_ptr<Media::Audio::Track> _callBusyTrack;

};

} // namespace Calls
