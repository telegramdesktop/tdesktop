/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_option.h"

#include "base/options.h"
#include "core/update_channel.h"

namespace Tgw {
namespace {

#ifdef _DEBUG
constexpr auto kDefaultEnabled = true;
#else
constexpr auto kDefaultEnabled = Core::BuildIsCanary;
#endif // _DEBUG

base::options::toggle OptionWidgetMessages({
	.id = kOptionWidgetMessages,
	.name = "HTML widget messages",
	.description = "Show HTML widget messages from bots "
		"as interactive widgets.",
	.defaultValue = kDefaultEnabled,
	.restartRequired = true,
});

auto EnabledAtStartup = false;

} // namespace

const char kOptionWidgetMessages[] = "html-widget-messages";

void InitEnabled() {
	EnabledAtStartup = OptionWidgetMessages.value();
}

bool Enabled() {
	return EnabledAtStartup;
}

} // namespace Tgw
