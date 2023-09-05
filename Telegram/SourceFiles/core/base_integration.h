/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
	void setCrashAnnotation(
		const std::string &key,
		const QString &value) override;

};

} // namespace Core
