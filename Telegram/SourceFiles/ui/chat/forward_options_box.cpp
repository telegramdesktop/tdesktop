/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/forward_options_box.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Ui {

void FillForwardOptions(
		Fn<not_null<AbstractCheckView*>(
			rpl::producer<QString> &&,
			bool)> createView,
		ForwardOptions options,
		Fn<void(ForwardOptions)> optionsChanged,
		rpl::lifetime &lifetime) {
	Expects(optionsChanged != nullptr);

	const auto names = createView(
		(options.sendersCount == 1
			? tr::lng_forward_show_sender
			: tr::lng_forward_show_senders)(),
		!options.dropNames);
	const auto captions = options.captionsCount
		? createView(
			(options.captionsCount == 1
				? tr::lng_forward_show_caption
				: tr::lng_forward_show_captions)(),
			!options.dropCaptions).get()
		: nullptr;

	const auto notify = [=] {
		optionsChanged({
			.sendersCount = options.sendersCount,
			.captionsCount = options.captionsCount,
			.dropNames = !names->checked(),
			.dropCaptions = (captions && !captions->checked()),
		});
	};
	names->checkedChanges(
	) | rpl::start_with_next([=](bool showNames) {
		if (showNames && captions && !captions->checked()) {
			captions->setChecked(true, anim::type::normal);
		} else {
			notify();
		}
	}, lifetime);
	if (captions) {
		captions->checkedChanges(
		) | rpl::start_with_next([=](bool showCaptions) {
			if (!showCaptions && names->checked()) {
				names->setChecked(false, anim::type::normal);
			} else {
				notify();
			}
		}, lifetime);
	}
}

} // namespace Ui
