/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	Shadow(
		QWidget *parent,
		const style::Shadow &st,
		RectParts sides = RectPart::AllSides)
	: TWidget(parent)
	, _st(st)
	, _sides(sides) {
	}

	static void paint(
		Painter &p,
		const QRect &box,
		int outerWidth,
		const style::Shadow &st,
		RectParts sides = RectPart::AllSides);

	static QPixmap grab(
		not_null<TWidget*> target,
		const style::Shadow &shadow,
		RectParts sides = RectPart::AllSides);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::Shadow &_st;
	RectParts _sides;

};

} // namespace Ui
