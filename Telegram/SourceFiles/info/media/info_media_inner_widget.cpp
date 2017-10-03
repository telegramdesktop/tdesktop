/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/media/info_media_inner_widget.h"

#include "ui/widgets/labels.h"

namespace Info {
namespace Media {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<PeerData*> peer,
	Type type)
: RpWidget(parent)
, _peer(peer)
, _type(type) {
	auto text = qsl("Media Overview\n\n");
	auto label = object_ptr<Ui::FlatLabel>(this);
	label->setText(text.repeated(50));
	widthValue() | rpl::start_with_next([inner = label.data()](int w) {
		inner->resizeToWidth(w);
	}, lifetime());
	label->heightValue() | rpl::start_with_next([this](int h) {
		_rowsHeightFake = h;
		resizeToWidth(width());
	}, lifetime());
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
}

int InnerWidget::resizeGetHeight(int newWidth) {
	return _rowsHeightFake;
}

} // namespace Media
} // namespace Info
