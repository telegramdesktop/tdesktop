/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_integration.h"

#ifdef Q_OS_MAC
#include "platform/mac/integration_mac.h"
#elif defined Q_OS_UNIX // Q_OS_MAC
#include "platform/linux/integration_linux.h"
#elif defined Q_OS_WINRT || defined Q_OS_WIN // Q_OS_MAC || Q_OS_UNIX
#include "platform/win/integration_win.h"
#endif // Q_OS_MAC || Q_OS_UNIX || Q_OS_WINRT || Q_OS_WIN

namespace Platform {
namespace {

Integration *GlobalInstance/* = nullptr*/;

} // namespace

Integration::~Integration() {
	GlobalInstance = nullptr;
}

std::unique_ptr<Integration> Integration::Create() {
	Expects(GlobalInstance == nullptr);

	auto result = CreateIntegration();
	GlobalInstance = result.get();
	return result;
}

Integration &Integration::Instance() {
	Expects(GlobalInstance != nullptr);

	return *GlobalInstance;
};

} // namespace Platform
