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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class PlainShadow : public TWidget {
public:
	PlainShadow(QWidget *parent, const style::color &color) : TWidget(parent), _color(color) {
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter(this).fillRect(e->rect(), _color->b);
	}

private:
	const style::color &_color;

};

class ToggleableShadow : public TWidget {
public:
	ToggleableShadow(QWidget *parent, const style::color &color) : TWidget(parent), _color(color) {
	}

	enum class Mode {
		Shown,
		ShownFast,
		Hidden,
		HiddenFast
	};
	void setMode(Mode mode);

	bool isFullyShown() const {
		return _shown && !_a_opacity.animating();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::color &_color;
	FloatAnimation _a_opacity;
	bool _shown = true;

};

class GradientShadow : public TWidget {
public:
	GradientShadow(QWidget *parent, const style::icon &icon) : TWidget(parent), _icon(icon) {
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return _icon.height();
	}
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		auto clip = e->rect();
		_icon.fill(p, QRect(clip.x(), 0, clip.width(), height()));
	}

private:
	const style::icon &_icon;

};

class Shadow : public TWidget {
public:
	enum class Side {
		Left = 0x01,
		Top = 0x02,
		Right = 0x04,
		Bottom = 0x08,
	};
	Q_DECLARE_FLAGS(Sides, Side);
	Q_DECLARE_FRIEND_OPERATORS_FOR_FLAGS(Sides);

	Shadow(QWidget *parent, const style::Shadow &st, Sides sides = Side::Left | Side::Top | Side::Right | Side::Bottom) : TWidget(parent)
	, _st(st)
	, _sides(sides) {
	}

	static void paint(Painter &p, const QRect &box, int outerWidth, const style::Shadow &st, Sides sides = Side::Left | Side::Top | Side::Right | Side::Bottom);

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		paint(p, rect().marginsRemoved(_st.extend), width(), _st, _sides);
	}

private:
	const style::Shadow &_st;
	Sides _sides;

};
Q_DECLARE_OPERATORS_FOR_FLAGS(Shadow::Sides);

} // namespace Ui
