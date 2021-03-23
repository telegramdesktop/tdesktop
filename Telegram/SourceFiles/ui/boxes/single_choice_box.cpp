/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/single_choice_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

void SingleChoiceBox(
		not_null<Ui::GenericBox*> box,
		SingleChoiceBoxArgs &&args) {
	box->setTitle(std::move(args.title));

	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		args.initialSelection);

	const auto layout = box->verticalLayout();
	layout->add(object_ptr<Ui::FixedHeightWidget>(
		layout,
		st::boxOptionListPadding.top() + st::autolockButton.margin.top()));
	auto &&ints = ranges::views::ints(0, ranges::unreachable);
	for (const auto &[i, text] : ranges::views::zip(ints, args.options)) {
		layout->add(
			object_ptr<Ui::Radiobutton>(
				layout,
				group,
				i,
				text,
				args.st ? *args.st : st::defaultBoxCheckbox,
				args.radioSt ? *args.radioSt : st::defaultRadio),
			QMargins(
				st::boxPadding.left() + st::boxOptionListPadding.left(),
				0,
				st::boxPadding.right(),
				st::boxOptionListSkip));
	}
	const auto callback = args.callback;
	group->setChangedCallback([=](int value) {
		const auto weak = Ui::MakeWeak(box);
		callback(value);
		if (weak) {
			box->closeBox();
		}
	});
}
