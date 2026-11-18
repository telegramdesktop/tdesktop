/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/glass/glass_option.h"

#include "base/options.h"

namespace Glass {
namespace {

base::options::toggle OptionLiquidGlass({
	.id = kOptionLiquidGlass,
	.name = "Liquid Glass surfaces",
	.description = "Use the Liquid Glass material foundation"
		" for theme-tinted surfaces.",
	.scope = base::options::windows,
	.restartRequired = true,
});

} // namespace

const char kOptionLiquidGlass[] = "liquid-glass";

bool Enabled() {
	return OptionLiquidGlass.value();
}

} // namespace Glass
