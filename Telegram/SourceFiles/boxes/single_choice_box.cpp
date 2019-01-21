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
	auto y = st::boxOptionListPadding.top()
		+ st::autolockButton.margin.top();
	_options.reserve(_optionTexts.size());
	auto i = 0;
	for (const auto &text : _optionTexts) {
		_options.emplace_back(this, group, i, text, st::autolockButton);
		_options.back()->moveToLeft(
			st::boxPadding.left() + st::boxOptionListPadding.left(),
			y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
		i++;
	}
	group->setChangedCallback([=](int value) {
		const auto weak = make_weak(this);
		_callback(value);
		if (weak) {
			closeBox();
		}
	});

	const auto height = y
		- st::boxOptionListSkip
		+ st::boxOptionListPadding.bottom()
		+ st::boxPadding.bottom();
	setDimensions(st::autolockWidth, height);
}

