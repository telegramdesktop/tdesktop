/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toasts/common_toasts.h"

#include "ui/toast/toast.h"
#include "styles/style_td_common.h"

namespace Ui {

base::weak_ptr<Toast::Instance> ShowMultilineToast(
		MultilineToastArgs &&args) {
	auto config = Ui::Toast::Config{
		.text = std::move(args.text),
		.st = &st::defaultMultilineToast,
		.durationMs = (args.duration
			? args.duration
			: Ui::Toast::kDefaultDuration),
		.multiline = true,
	};
	return args.parentOverride
		? Ui::Toast::Show(args.parentOverride, std::move(config))
		: Ui::Toast::Show(std::move(config));
}

} // namespace Ui
