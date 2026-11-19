/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstddef>

namespace WidgetHelper {

inline constexpr auto kThreadStackBytes = std::size_t(8) << 20;

[[nodiscard]] int StartDetachedThread(void *(*entry)(void*), void *argument);

} // namespace WidgetHelper
