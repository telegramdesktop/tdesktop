/*
 This file is part of Telegram Desktop,
 an unofficial desktop messaging app, see https://telegram.org

 Telegram Desktop is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 It is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
 Copyright (c) 2014 John Preston, https://tdesktop.com
 */
#include "stdafx.h"

#include "contextmenu.h"
#include "flatbutton.h"

#include "lang.h"

ContextMenu::ContextMenu(QWidget *parent) : QWidget(parent),
_hiding(false), a_opacity(0) {
	resetButtons();

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	hide();
}

FlatButton *ContextMenu::addButton(FlatButton *button) {
	button->setParent(this);

	_width = qMax(_width, int(2 * st::dropdownBorder + button->width()));
	if (!_buttons.isEmpty()) {
		_height += st::dropdownBorder;
	}
	_height += button->height();

	_buttons.push_back(button);

	resize(_width, _height);

	return button;
}

void ContextMenu::resetButtons() {
	_width = 2 * st::dropdownBorder;
	_height = 2 * st::dropdownBorder;
	resize(_width, _height);
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
		delete _buttons[i];
	}
	_buttons.clear();
}

void ContextMenu::resizeEvent(QResizeEvent *e) {
	int32 top = st::dropdownBorder;
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->move(st::dropdownBorder, top);
		top += st::dropdownBorder + (*i)->height();
	}
}

void ContextMenu::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	// paint window border
	p.fillRect(QRect(0, 0, _width - st::dropdownBorder, st::dropdownBorder), st::dropdownBorderColor->b);
	p.fillRect(QRect(_width - st::dropdownBorder, 0, st::dropdownBorder, _height - st::dropdownBorder), st::dropdownBorderColor->b);
	p.fillRect(QRect(st::dropdownBorder, _height - st::dropdownBorder, _width - st::dropdownBorder, st::dropdownBorder), st::dropdownBorderColor->b);
	p.fillRect(QRect(0, st::dropdownBorder, st::dropdownBorder, _height - st::dropdownBorder), st::dropdownBorderColor->b);

	if (!_buttons.isEmpty()) { // paint separators
		int32 top = st::dropdownBorder + _buttons.front()->height();
		p.setPen(st::dropdownBorderColor->p);
		for (int32 i = 1, s = _buttons.size(); i < s; ++i) {
			p.fillRect(st::dropdownBorder, top, _width - 2 * st::dropdownBorder, st::dropdownBorder, st::dropdownBorderColor->b);
			top += st::dropdownBorder + _buttons[i]->height();
		}
	}
}

void ContextMenu::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	hide();
}

void ContextMenu::adjustButtons() {
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->setOpacity(a_opacity.current());
	}
}

void ContextMenu::hideStart() {
	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void ContextMenu::hideFinish() {
	hide();
}

void ContextMenu::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	_hiding = false;
	show();
	a_opacity.start(1);
	anim::start(this);
}

bool ContextMenu::animStep(float64 ms) {
	float64 dt = ms / 150;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		if (_hiding) {
			hideFinish();
		}
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
	}
	adjustButtons();
	update();
	return res;
}
