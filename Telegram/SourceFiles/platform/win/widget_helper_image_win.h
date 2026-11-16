/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/win/widget_channel_win.h"

#include <string>
#include <vector>

namespace Platform::WidgetHelper {

struct HeldHelper {
	std::wstring path;
	std::wstring native;
	std::vector<UniqueHandle> folders;
	UniqueHandle file;
	FILE_ID_INFO identity = {};
	std::string error;
};

[[nodiscard]] HeldHelper HoldHelper();
[[nodiscard]] std::string CheckStartedImage(
	const HeldHelper &held,
	HANDLE process);

} // namespace Platform::WidgetHelper
