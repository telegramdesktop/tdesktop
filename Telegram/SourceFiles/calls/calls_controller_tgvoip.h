/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/calls_controller.h"

namespace Calls {

class TgVoipController final : public Controller {
public:
	TgVoipController(
		const TgVoipConfig &config,
		const TgVoipPersistentState &persistentState,
		const std::vector<TgVoipEndpoint> &endpoints,
		const TgVoipProxy *proxy,
		TgVoipNetworkType initialNetworkType,
		const TgVoipEncryptionKey &encryptionKey)
	: _impl(TgVoip::makeInstance(
		config,
		persistentState,
		endpoints,
		proxy,
		initialNetworkType,
		encryptionKey)) {
	}

	[[nodiscard]] static std::string Version() {
		return TgVoip::getVersion();
	}

	std::string version() override {
		return Version();
	}
	void setNetworkType(TgVoipNetworkType networkType) override {
		_impl->setNetworkType(networkType);
	}
	void setMuteMicrophone(bool muteMicrophone) override {
		_impl->setMuteMicrophone(muteMicrophone);
	}
	void setAudioOutputGainControlEnabled(bool enabled) override {
		_impl->setAudioOutputGainControlEnabled(enabled);
	}
	void setEchoCancellationStrength(int strength) override {
		_impl->setEchoCancellationStrength(strength);
	}
	void setAudioInputDevice(std::string id) override {
		_impl->setAudioInputDevice(id);
	}
	void setAudioOutputDevice(std::string id) override {
		_impl->setAudioOutputDevice(id);
	}
	void setInputVolume(float level) override {
		_impl->setInputVolume(level);
	}
	void setOutputVolume(float level) override {
		_impl->setOutputVolume(level);
	}
	void setAudioOutputDuckingEnabled(bool enabled) override {
		_impl->setAudioOutputDuckingEnabled(enabled);
	}
	bool receiveSignalingData(const QByteArray &data) override {
		return false;
	}
	std::string getLastError() override {
		return _impl->getLastError();
	}
	std::string getDebugInfo() override {
		return _impl->getDebugInfo();
	}
	int64_t getPreferredRelayId() override {
		return _impl->getPreferredRelayId();
	}
	TgVoipTrafficStats getTrafficStats() override {
		return _impl->getTrafficStats();
	}
	TgVoipPersistentState getPersistentState() override {
		return _impl->getPersistentState();
	}
	void setOnStateUpdated(Fn<void(TgVoipState)> onStateUpdated) override {
		_impl->setOnStateUpdated(std::move(onStateUpdated));
	}
	void setOnSignalBarsUpdated(Fn<void(int)> onSignalBarsUpdated) override {
		_impl->setOnSignalBarsUpdated(std::move(onSignalBarsUpdated));
	}
	TgVoipFinalState stop() override {
		return _impl->stop();
	}

private:
	const std::unique_ptr<TgVoip> _impl;

};

} // namespace Calls
