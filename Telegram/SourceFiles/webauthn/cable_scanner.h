/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

#include <QtCore/QByteArray>

#include <functional>
#include <memory>

namespace Platform::WebAuthn::Cable {

class BleScanner {
public:
	virtual ~BleScanner() = default;

	[[nodiscard]] virtual bool start(
		std::function<void(QByteArray)> onAdvert,
		std::function<void()> onUnavailable) = 0;
	virtual void stop() = 0;
};

[[nodiscard]] std::unique_ptr<BleScanner> MakeBleScanner();

} // namespace Platform::WebAuthn::Cable
