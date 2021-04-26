/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/base_integration.h"

#include "core/sandbox.h"
#include "core/crash_reports.h"

namespace Core {

BaseIntegration::BaseIntegration(int argc, char *argv[])
: Integration(argc, argv) {
}

void BaseIntegration::enterFromEventLoop(FnMut<void()> &&method) {
	Core::Sandbox::Instance().customEnterFromEventLoop(
		std::move(method));
}

bool BaseIntegration::logSkipDebug() {
	return !Logs::DebugEnabled() && Logs::started();
}

void BaseIntegration::logMessageDebug(const QString &message) {
	Logs::writeDebug(message);
}

void BaseIntegration::logMessage(const QString &message) {
	Logs::writeMain(message);
}

void BaseIntegration::logAssertionViolation(const QString &info) {
	Logs::writeMain("Assertion Failed! " + info);
	CrashReports::SetAnnotation("Assertion", info);
}

} // namespace Core
