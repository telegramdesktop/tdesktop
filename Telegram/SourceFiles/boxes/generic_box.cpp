/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/generic_box.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/wrap.h"
#include "styles/style_boxes.h"

void GenericBox::prepare() {
	_init(this);

	auto wrap = object_ptr<Ui::OverrideMargins>(this, std::move(_content));
	setDimensionsToContent(_width ? _width : st::boxWidth, wrap.data());
	setInnerWidget(std::move(wrap));
}

void GenericBox::addSkip(int height) {
	addRow(object_ptr<Ui::FixedHeightWidget>(this, height));
}
