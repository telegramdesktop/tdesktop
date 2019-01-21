/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/single_choice_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_boxes.h"

SingleChoiceBox::SingleChoiceBox(
	QWidget*,
	LangKey title,
	const std::vector<QString> &optionTexts,
	int initialSelection,
	Fn<void(int)> callback)
: _title(title)
, _optionTexts(optionTexts)
, _initialSelection(initialSelection)
, _callback(callback) {
}

void SingleChoiceBox::prepare() {
	setTitle(langFactory(_title));

	addButton(langFactory(lng_box_ok), [=] { closeBox(); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(_initialSelection);

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::boxOptionListPadding.top() + st::autolockButton.margin.top()));
	auto &&ints = ranges::view::ints(0);
	for (const auto &[i, text] : ranges::view::zip(ints, _optionTexts)) {
		content->add(
			object_ptr<Ui::Radiobutton>(
				content,
				group,
				i,
				text,
				st::defaultBoxCheckbox),
			QMargins(
				st::boxPadding.left() + st::boxOptionListPadding.left(),
				0,
				st::boxPadding.right(),
				st::boxOptionListSkip));
	}
	group->setChangedCallback([=](int value) {
		const auto weak = make_weak(this);
		_callback(value);
		if (weak) {
			closeBox();
		}
	});
	setDimensionsToContent(st::boxWidth, content);
}

