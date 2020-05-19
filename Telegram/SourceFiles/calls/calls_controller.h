/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "TgVoip.h"

namespace Calls {

class Controller {
public:
	virtual ~Controller() = default;

	[[nodiscard]] virtual std::string version() = 0;

	virtual void setNetworkType(TgVoipNetworkType networkType) = 0;
	virtual void setMuteMicrophone(bool muteMicrophone) = 0;
	virtual void setAudioOutputGainControlEnabled(bool enabled) = 0;
	virtual void setEchoCancellationStrength(int strength) = 0;
	virtual void setAudioInputDevice(std::string id) = 0;
	virtual void setAudioOutputDevice(std::string id) = 0;
	virtual void setInputVolume(float level) = 0;
	virtual void setOutputVolume(float level) = 0;
	virtual void setAudioOutputDuckingEnabled(bool enabled) = 0;
	virtual bool receiveSignalingData(const QByteArray &data) = 0;

	virtual std::string getLastError() = 0;
	virtual std::string getDebugInfo() = 0;
	virtual int64_t getPreferredRelayId() = 0;
	virtual TgVoipTrafficStats getTrafficStats() = 0;
	virtual TgVoipPersistentState getPersistentState() = 0;

	virtual void setOnStateUpdated(Fn<void(TgVoipState)> onStateUpdated) = 0;
	virtual void setOnSignalBarsUpdated(
		Fn<void(int)> onSignalBarsUpdated) = 0;

	virtual TgVoipFinalState stop() = 0;

};

[[nodiscard]] std::unique_ptr<Controller> MakeController(
	const std::string &version,
	const TgVoipConfig &config,
	const TgVoipPersistentState &persistentState,
	const std::vector<TgVoipEndpoint> &endpoints,
	const TgVoipProxy *proxy,
	TgVoipNetworkType initialNetworkType,
	const TgVoipEncryptionKey &encryptionKey,
	Fn<void(QByteArray)> sendSignalingData,
	Fn<void(QImage)> displayNextFrame);

[[nodiscard]] std::vector<std::string> CollectControllerVersions();
[[nodiscard]] int ControllerMaxLayer();

} // namespace Calls
