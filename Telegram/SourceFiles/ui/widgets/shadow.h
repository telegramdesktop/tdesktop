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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct Shadow;
} // namespace style

namespace Ui {

class PlainShadow : public RpWidget {
public:
	PlainShadow(QWidget *parent);
	PlainShadow(QWidget *parent, style::color color);

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter(this).fillRect(e->rect(), _color);
	}

private:
	style::color _color;

};

class Shadow : public TWidget {
public:
	Shadow(QWidget *parent, const style::Shadow &st, RectParts sides = RectPart::Left | RectPart::Top | RectPart::Right | RectPart::Bottom) : TWidget(parent)
	, _st(st)
	, _sides(sides) {
	}

	static void paint(Painter &p, const QRect &box, int outerWidth, const style::Shadow &st, RectParts sides = RectPart::Left | RectPart::Top | RectPart::Right | RectPart::Bottom);

	static QPixmap grab(TWidget *target, const style::Shadow &shadow, RectParts sides = RectPart::Left | RectPart::Top | RectPart::Right | RectPart::Bottom);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::Shadow &_st;
	RectParts _sides;

};

} // namespace Ui
