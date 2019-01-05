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

void SingleChoiceBox::prepare() {
	setTitle(langFactory(_title));

	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	auto group = std::make_shared<Ui::RadiobuttonGroup>(_initialSelection);
	auto y = st::boxOptionListPadding.top() + st::autolockButton.margin.top();
	auto count = int(_optionTexts.size());
	_options.reserve(count);
	auto i = 0;
	for (const auto &text : _optionTexts) {
		_options.emplace_back(this, group, i, text, st::autolockButton);
		_options.back()->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
		i++;
	}
	group->setChangedCallback([this](int value) {
		_callback(value);
		closeBox();
	});

	setDimensions(st::autolockWidth, st::boxOptionListPadding.top() + count * _options.back()->heightNoMargins() + (count - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

