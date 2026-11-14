/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "platform/posix/widget_channel_posix.h"

#include <string>
#include <unistd.h>

namespace WidgetHelper {

std::unique_ptr<twidget::SandboxRemoteStream> Adopt(std::string_view token) {
	constexpr auto kDescriptor = Platform::WidgetHelper::kChannelDescriptor;
	if (token != std::to_string(kDescriptor) || chdir("/") != 0) {
		return nullptr;
	}
	return Platform::WidgetHelper::AdoptPosixChannel(kDescriptor);
}

} // namespace WidgetHelper
