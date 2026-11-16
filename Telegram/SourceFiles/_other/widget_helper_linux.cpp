/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "platform/linux/widget_lockdown_linux.h"
#include "platform/posix/widget_channel_posix.h"

#include <sys/prctl.h>

#include <string>

#ifdef __GLIBC__
#include <malloc.h>
#endif // __GLIBC__

namespace WidgetHelper {

std::unique_ptr<twidget::SandboxRemoteStream> Adopt(std::string_view token) {
	constexpr auto kDescriptor = Platform::WidgetHelper::kChannelDescriptor;
	constexpr auto kProcessName = "TelegramWidgetHelper";
	if (token != std::to_string(kDescriptor)) {
		return nullptr;
	}
	prctl(PR_SET_NAME, kProcessName);
#ifdef __GLIBC__
	mallopt(M_ARENA_MAX, 1);
#endif // __GLIBC__
	Platform::WidgetHelper::PrepareHelperProcess(kDescriptor + 1);
	return Platform::WidgetHelper::AdoptPosixChannel(kDescriptor);
}

std::string Lockdown() {
	return Platform::WidgetHelper::LockDownHelper();
}

} // namespace WidgetHelper
