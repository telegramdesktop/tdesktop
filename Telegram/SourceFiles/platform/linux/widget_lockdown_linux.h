/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef __linux__

#include <string>

namespace Platform::WidgetHelper {

#if defined __x86_64__ || defined __aarch64__
inline constexpr auto kLockdownAvailable = true;
#else // __x86_64__ || __aarch64__
inline constexpr auto kLockdownAvailable = false;
#endif // __x86_64__ || __aarch64__

void PrepareHelperProcess(int firstToClose);
[[nodiscard]] std::string LockDownHelper();

} // namespace Platform::WidgetHelper

#endif // __linux__
