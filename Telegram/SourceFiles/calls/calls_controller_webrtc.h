/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/calls_controller.h"

namespace Webrtc {
class CallContext;
} // namespace Webrtc

namespace Calls {

class WebrtcController final : public Controller {
public:
	WebrtcController(
		const TgVoipConfig &config,
		const TgVoipPersistentState &persistentState,
		const std::vector<TgVoipEndpoint> &endpoints,
		const TgVoipProxy *proxy,
		TgVoipNetworkType initialNetworkType,
		const TgVoipEncryptionKey &encryptionKey,
		Fn<void(QByteArray)> sendSignalingData,
		Fn<void(QImage)> displayNextFrame);
	~WebrtcController();

	[[nodiscard]] static std::string Version();

	std::string version() override;
	void setNetworkType(TgVoipNetworkType networkType) override;
	void setMuteMicrophone(bool muteMicrophone) override;
	void setAudioOutputGainControlEnabled(bool enabled) override;
	void setEchoCancellationStrength(int strength) override;
	void setAudioInputDevice(std::string id) override;
	void setAudioOutputDevice(std::string id) override;
	void setInputVolume(float level) override;
	void setOutputVolume(float level) override;
	void setAudioOutputDuckingEnabled(bool enabled) override;
	bool receiveSignalingData(const QByteArray &data) override;
	std::string getLastError() override;
	std::string getDebugInfo() override;
	int64_t getPreferredRelayId() override;
	TgVoipTrafficStats getTrafficStats() override;
	TgVoipPersistentState getPersistentState() override;
	void setOnStateUpdated(Fn<void(TgVoipState)> onStateUpdated) override;
	void setOnSignalBarsUpdated(Fn<void(int)> onSignalBarsUpdated) override;
	TgVoipFinalState stop() override;

private:
	const std::unique_ptr<Webrtc::CallContext> _impl;

	rpl::lifetime _stateUpdatedLifetime;

};

} // namespace Calls
