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
		int count,
		ForwardOptions options,
		Fn<void(ForwardOptions)> optionsChanged,
		rpl::lifetime &lifetime) {
	Expects(optionsChanged != nullptr);

	const auto names = createView(
		(count == 1
			? tr::lng_forward_show_sender
			: tr::lng_forward_show_senders)(),
		!options.dropNames);
	const auto captions = options.hasCaptions
		? createView(
			(count == 1
				? tr::lng_forward_show_caption
				: tr::lng_forward_show_captions)(),
			!options.dropCaptions).get()
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

	auto createView = [&](rpl::producer<QString> &&text, bool checked) {
		return box->addRow(
			object_ptr<Ui::Checkbox>(
				box.get(),
				std::move(text),
				checked,
				st::defaultBoxCheckbox),
			checkboxPadding)->checkView();
	};
	FillForwardOptions(
		std::move(createView),
		count,
		options,
		std::move(optionsChanged),
		box->lifetime());

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
