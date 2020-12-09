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
#include "styles/style_layers.h"

SingleChoiceBox::SingleChoiceBox(
	QWidget*,
	rpl::producer<QString> title,
	const std::vector<QString> &optionTexts,
	int initialSelection,
	Fn<void(int)> callback,
	const style::Checkbox *st,
	const style::Radio *radioSt)
: _title(std::move(title))
, _optionTexts(optionTexts)
, _initialSelection(initialSelection)
, _callback(callback)
, _st(st ? *st : st::defaultBoxCheckbox)
, _radioSt(radioSt ? *radioSt : st::defaultRadio) {
}

void SingleChoiceBox::prepare() {
	setTitle(std::move(_title));

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(_initialSelection);

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::boxOptionListPadding.top() + st::autolockButton.margin.top()));
	auto &&ints = ranges::view::ints(0, ranges::unreachable);
	for (const auto &[i, text] : ranges::view::zip(ints, _optionTexts)) {
		content->add(
			object_ptr<Ui::Radiobutton>(
				content,
				group,
				i,
				text,
				_st,
				_radioSt),
			QMargins(
				st::boxPadding.left() + st::boxOptionListPadding.left(),
				0,
				st::boxPadding.right(),
				st::boxOptionListSkip));
	}
	group->setChangedCallback([=](int value) {
		const auto weak = Ui::MakeWeak(this);
		_callback(value);
		if (weak) {
			closeBox();
		}
	});
	setDimensionsToContent(st::boxWidth, content);
}

