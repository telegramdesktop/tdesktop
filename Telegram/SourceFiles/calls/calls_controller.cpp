/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_controller.h"

#include "calls/calls_controller_tgvoip.h"
#include "calls/calls_controller_webrtc.h"

namespace Calls {

[[nodiscard]] std::unique_ptr<Controller> MakeController(
		const std::string &version,
		const TgVoipConfig &config,
		const TgVoipPersistentState &persistentState,
		const std::vector<TgVoipEndpoint> &endpoints,
		const TgVoipProxy *proxy,
		TgVoipNetworkType initialNetworkType,
		const TgVoipEncryptionKey &encryptionKey,
		Fn<void(QByteArray)> sendSignalingData,
		Fn<void(QImage)> displayNextFrame) {
	if (version == WebrtcController::Version()) {
		return std::make_unique<WebrtcController>(
			config,
			persistentState,
			endpoints,
			proxy,
			initialNetworkType,
			encryptionKey,
			std::move(sendSignalingData),
			std::move(displayNextFrame));
	}
	return std::make_unique<TgVoipController>(
		config,
		persistentState,
		endpoints,
		proxy,
		initialNetworkType,
		encryptionKey);
}

std::vector<std::string> CollectControllerVersions() {
	return { WebrtcController::Version(), TgVoipController::Version() };
}

int ControllerMaxLayer() {
	return TgVoip::getConnectionMaxLayer();
}

} // namespace Calls
