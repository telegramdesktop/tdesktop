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
 Copyright (c) 2014 John Preston, https://desktop.telegram.org
 */
#include "stdafx.h"

#include "contextmenu.h"
#include "flatbutton.h"
#include "pspecific.h"

#include "lang.h"

ContextMenu::ContextMenu(QWidget *parent, const style::iconedButton &st) : TWidget(0),
_hiding(false), _buttonStyle(st), _shadow(st::dropdownShadow), _selected(-1), a_opacity(0), _deleteOnHide(false) {
	resetActions();

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

QAction *ContextMenu::addAction(const QString &text, const QObject *receiver, const char* member) {
	QAction *a = 0;
	_actions.push_back(a = new QAction(text, this));
	connect(a, SIGNAL(triggered(bool)), receiver, member);
	connect(a, SIGNAL(changed()), this, SLOT(actionChanged()));

	IconedButton *b = 0;
	_buttons.push_back(b = new IconedButton(this, _buttonStyle, a->text()));
	connect(b, SIGNAL(clicked()), this, SLOT(hideStart()));
	connect(b, SIGNAL(clicked()), a, SIGNAL(triggered()));
	connect(b, SIGNAL(stateChanged(int,ButtonStateChangeSource)), this, SLOT(buttonStateChanged(int,ButtonStateChangeSource)));

	_width = qMax(_width, int(st::dropdownPadding.left() + st::dropdownPadding.right() + b->width()));
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) _buttons[i]->resize(_width - int(st::dropdownPadding.left() + st::dropdownPadding.right()), _buttons[i]->height());
	_height += b->height();

	resize(_width, _height);

	return a;
}

ContextMenu::Actions &ContextMenu::actions() {
	return _actions;
}

void ContextMenu::actionChanged() {
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		_buttons[i]->setText(_actions[i]->text());
		_width = qMax(_width, int(st::dropdownPadding.left() + st::dropdownPadding.right() + _buttons[i]->width()));
		_buttons[i]->resize(_width - int(st::dropdownPadding.left() + st::dropdownPadding.right()), _buttons[i]->height());
	}
}

void ContextMenu::onActiveChanged() {
	if (!windowHandle()->isActive()) {
		hideStart();
	}
}

void ContextMenu::buttonStateChanged(int oldState, ButtonStateChangeSource source) {
	if (source == ButtonByUser) {
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				if (i != _selected) {
					_buttons[i]->setOver(false);
				}
			}
		}
	} else if (source == ButtonByHover) {
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				if (i != _selected) {
					int32 sel = _selected;
					_selected = i;
					if (sel >= 0 && sel < _buttons.size()) {
						_buttons[sel]->setOver(false);
					}
				}
			}
		}
	}
}

void ContextMenu::resetActions() {
	_width = st::dropdownPadding.left() + st::dropdownPadding.right();
	_height = st::dropdownPadding.top() + st::dropdownPadding.bottom();
	resize(_width, _height);

	clearActions();
}

void ContextMenu::clearActions() {
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
		delete _buttons[i];
	}
	_buttons.clear();

	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		delete _actions[i];
	}
	_actions.clear();

	_selected = -1;
}

void ContextMenu::resizeEvent(QResizeEvent *e) {
	int32 top = st::dropdownPadding.top();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->move(st::dropdownPadding.left(), top);
		top += (*i)->height();
	}
}

void ContextMenu::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(e->rect(), st::transparent->b);
	p.setCompositionMode(m);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dropdownPadding.left(), st::dropdownPadding.top(), _width - st::dropdownPadding.left() - st::dropdownPadding.right(), _height - st::dropdownPadding.top() - st::dropdownPadding.bottom());
	// draw shadow
	_shadow.paint(p, r);
}

void ContextMenu::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_selected >= 0 && _selected < _buttons.size()) {
			emit _buttons[_selected]->clicked();
			return;
		}
	}
	if ((e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) || _buttons.size() < 1) return;

	int32 newSelected = _selected + (e->key() == Qt::Key_Down ? 1 : -1);
	if (_selected < 0 || _selected >= _buttons.size()) {
		newSelected = e->key() == Qt::Key_Down ? 0 : (_buttons.size() - 1);
	} else {
		if (newSelected < 0) {
			newSelected = _buttons.size() - 1;
		} else if (newSelected >= _buttons.size()) {
			newSelected = 0;
		}
		_buttons[_selected]->setOver(false);
	}
	_selected = newSelected;
	_buttons[_selected]->setOver(true);
}

void ContextMenu::focusOutEvent(QFocusEvent *e) {
	if (!_hiding) hideStart();
}

void ContextMenu::fastHide() {
	if (animating()) {
		anim::stop(this);
	}
	a_opacity = anim::fvalue(0, 0);
	hideFinish();
}

void ContextMenu::adjustButtons() {
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->setOpacity(a_opacity.current());
	}
}

void ContextMenu::hideStart() {
	if (isHidden()) return;

	_hiding = true;
	a_opacity.start(0);
	anim::start(this);
}

void ContextMenu::hideFinish() {
	hide();
	if (_deleteOnHide) {
		deleteLater();
	}
}

void ContextMenu::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	_selected = -1;
	_hiding = false;
	a_opacity.start(1);
	anim::start(this);
	animStep(0);
	psUpdateOverlayed(this);
	show();
	psShowOverAll(this);
	windowHandle()->requestActivate();
	activateWindow();
	setFocus();
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

void ContextMenu::deleteOnHide() {
	_deleteOnHide = true;
}

void ContextMenu::popup(const QPoint &p) {
	QPoint w = p - QPoint(st::dropdownPadding.left(), st::dropdownPadding.top());
	QRect r = QDesktopWidget().screenGeometry(p);
	if (w.x() + width() - st::dropdownPadding.right() > r.x() + r.width()) {
		w.setX(r.x() + r.width() - width() + st::dropdownPadding.right());
	}
	if (w.y() + height() - st::dropdownPadding.bottom() > r.y() + r.height()) {
		w.setY(p.y() - height() + st::dropdownPadding.bottom());
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);
	showStart();
}

ContextMenu::~ContextMenu() {
	clearActions();
}
