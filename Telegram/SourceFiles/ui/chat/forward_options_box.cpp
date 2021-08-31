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

void ForwardOptionsBox(
		not_null<GenericBox*> box,
		int count,
		ForwardOptions options,
		Fn<void(ForwardOptions)> optionsChanged,
		Fn<void()> changeRecipient) {
	Expects(optionsChanged != nullptr);
	Expects(changeRecipient != nullptr);

	box->setTitle((count == 1)
		? tr::lng_forward_title()
		: tr::lng_forward_many_title(
			lt_count,
			rpl::single(count) | tr::to_count()));
	box->addButton(tr::lng_box_done(), [=] {
		box->closeBox();
	});
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			(count == 1
				? tr::lng_forward_about()
				: tr::lng_forward_many_about()),
			st::boxLabel),
		st::boxRowPadding);
	const auto checkboxPadding = style::margins(
		st::boxRowPadding.left(),
		st::boxRowPadding.left(),
		st::boxRowPadding.right(),
		st::boxRowPadding.bottom());
	const auto names = box->addRow(
		object_ptr<Ui::Checkbox>(
			box.get(),
			(count == 1
				? tr::lng_forward_show_sender
				: tr::lng_forward_show_senders)(),
			!options.dropNames,
			st::defaultBoxCheckbox),
		checkboxPadding);
	const auto captions = options.hasCaptions
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box.get(),
				(count == 1
					? tr::lng_forward_show_caption
					: tr::lng_forward_show_captions)(),
				!options.dropCaptions,
				st::defaultBoxCheckbox),
			checkboxPadding)
		: nullptr;
	const auto notify = [=] {
		optionsChanged({
			.dropNames = !names->checked(),
			.hasCaptions = options.hasCaptions,
			.dropCaptions = (captions && !captions->checked()),
		});
	};
	names->checkedChanges(
	) | rpl::start_with_next([=](bool showNames) {
		if (showNames && captions && !captions->checked()) {
			captions->setChecked(true);
		} else {
			notify();
		}
	}, names->lifetime());
	if (captions) {
		captions->checkedChanges(
		) | rpl::start_with_next([=](bool showCaptions) {
			if (!showCaptions && names->checked()) {
				names->setChecked(false);
			} else {
				notify();
			}
		}, captions->lifetime());
	}
	box->addRow(
		object_ptr<Ui::LinkButton>(
			box.get(),
			tr::lng_forward_change_recipient(tr::now)),
		checkboxPadding
	)->setClickedCallback([=] {
		box->closeBox();
		changeRecipient();
	});
}

} // namespace Ui
