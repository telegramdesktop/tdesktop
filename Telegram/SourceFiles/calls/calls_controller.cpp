/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_controller.h"

#include "calls/calls_controller_tgvoip.h"

namespace Calls {

[[nodiscard]] std::unique_ptr<Controller> MakeController(
		const std::string &version,
		const TgVoipConfig &config,
		const TgVoipPersistentState &persistentState,
		const std::vector<TgVoipEndpoint> &endpoints,
		const TgVoipProxy *proxy,
		TgVoipNetworkType initialNetworkType,
		const TgVoipEncryptionKey &encryptionKey) {
	return std::make_unique<TgVoipController>(
		config,
		persistentState,
		endpoints,
		proxy,
		initialNetworkType,
		encryptionKey);
}

} // namespace Calls
