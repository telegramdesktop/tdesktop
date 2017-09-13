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
#include "info/info_top_bar.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

namespace Info {

TopBar::TopBar(QWidget *parent, const style::InfoTopBar &st)
: RpWidget(parent)
, _st(st) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	_title.create(this, std::move(title), _st.title);
}

void TopBar::enableBackButton(bool enable) {
	if (enable) {
		_back.create(this, _st.back);
		_back->clicks()
			| rpl::to_stream(_backClicks)
			| rpl::start(_lifetime);
	} else {
		_back.destroy();
	}
	if (_title) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents, enable);
	}
	updateControlsGeometry(width());
}

void TopBar::pushButton(object_ptr<Ui::RpWidget> button) {
	auto weak = Ui::AttachParentChild(this, button);
	_buttons.push_back(std::move(button));
	weak->widthValue()
		| rpl::on_next([this](auto) {
			updateControlsGeometry(width());
		})
		| rpl::start(_lifetime);
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_back) {
		_back->setGeometryToLeft(0, 0, newWidth - right, _back->height(), newWidth);
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _st.bg);
}

} // namespace Info
