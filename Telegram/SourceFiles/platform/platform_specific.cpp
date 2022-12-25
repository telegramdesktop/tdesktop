/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_specific.h"

#include "base/options.h"

namespace Platform {

const char kOptionGApplication[] = "gapplication";

base::options::toggle OptionGApplication({
	.id = kOptionGApplication,
	.name = "GApplication",
	.description = "Force enable GLib's GApplication and GNotification."
		" When disabled, autodetect is used.",
	.scope = base::options::linux,
	.restartRequired = true,
});

} // namespace Platform
