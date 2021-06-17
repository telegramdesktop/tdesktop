/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/integration.h"

namespace Core {

class BaseIntegration final : public base::Integration {
public:
	BaseIntegration(int argc, char *argv[]);

	void enterFromEventLoop(FnMut<void()> &&method) override;
	bool logSkipDebug() override;
	void logMessageDebug(const QString &message) override;
	void logMessage(const QString &message) override;
	void logAssertionViolation(const QString &info) override;

};

} // namespace Core
