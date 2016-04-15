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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/toast/toast_widget.h"

namespace Ui {
namespace Toast {
namespace internal {

Widget::Widget(QWidget *parent, const Config &config) : TWidget(parent) {
	TextParseOptions toastOptions = { 0, int(st::toastMaxWidth), st::toastFont->height, Qt::LayoutDirectionAuto };
	_text.setText(st::toastFont, textOneLine(config.text), toastOptions);

	setAttribute(Qt::WA_TransparentForMouseEvents);

	onParentResized();
	show();
}

void Widget::onParentResized() {
	int width = st::toastMaxWidth;
	accumulate_min(width, st::toastPadding.left() + _text.maxWidth() + st::toastPadding.right());
	accumulate_min(width, parentWidget()->width() - 2 * int(st::toastMinMargin));
	int height = st::toastPadding.top() + _text.minHeight() + st::toastPadding.bottom();
	setGeometry((parentWidget()->width() - width) / 2, (parentWidget()->height() - height) / 2, width, height);
}

void Widget::setShownLevel(float64 shownLevel) {
	_shownLevel = shownLevel;
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setOpacity(_shownLevel);
	App::roundRect(p, rect(), st::toastBg);

	p.setPen(st::toastFg);
	textstyleSet(&st::defaultTextStyle);
	_text.drawElided(p, st::toastPadding.left(), st::toastPadding.top(), width() - st::toastPadding.left() - st::toastPadding.right());
	textstyleRestore();
}

} // namespace internal
} // namespace Toast
} // namespace Ui
