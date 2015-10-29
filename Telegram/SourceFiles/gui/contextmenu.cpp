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
 Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
 */
#include "stdafx.h"

#include "contextmenu.h"
#include "flatbutton.h"
#include "pspecific.h"

#include "application.h"

#include "lang.h"

ContextMenu::ContextMenu(QWidget *parent, const style::dropdown &st, const style::iconedButton &btnst) : TWidget(0),
_width(st.width), _hiding(false), _st(st), _btnst(btnst), _shadow(_st.shadow), _selected(-1), a_opacity(0), _deleteOnHide(false) {
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
	_buttons.push_back(b = new IconedButton(this, _btnst, a->text()));
	connect(b, SIGNAL(clicked()), this, SLOT(hideStart()));
	connect(b, SIGNAL(clicked()), a, SIGNAL(triggered()));
	connect(b, SIGNAL(stateChanged(int,ButtonStateChangeSource)), this, SLOT(buttonStateChanged(int,ButtonStateChangeSource)));

	_width = qMax(_width, int(_st.padding.left() + _st.padding.right() + b->width()));
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) _buttons[i]->resize(_width - int(_st.padding.left() + _st.padding.right()), _buttons[i]->height());
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
		_width = qMax(_width, int(_st.padding.left() + _st.padding.right() + _buttons[i]->width()));
		_buttons[i]->resize(_width - int(_st.padding.left() + _st.padding.right()), _buttons[i]->height());
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
	_width = qMax(_st.padding.left() + _st.padding.right(), int(_st.width));
	_height = _st.padding.top() + _st.padding.bottom();
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
	int32 top = _st.padding.top();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->move(_st.padding.left(), top);
		top += (*i)->height();
	}
}

void ContextMenu::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.setClipRect(e->rect());
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(e->rect(), st::transparent->b);
	p.setCompositionMode(m);

	if (animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(_st.padding.left(), _st.padding.top(), _width - _st.padding.left() - _st.padding.right(), _height - _st.padding.top() - _st.padding.bottom());
	// draw shadow
	_shadow.paint(p, r, _st.shadowShift);
}

void ContextMenu::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_selected >= 0 && _selected < _buttons.size()) {
			emit _buttons[_selected]->clicked();
			return;
		}
	} else if (e->key() == Qt::Key_Escape) {
		hideStart();
		return;
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
	QPoint w = p - QPoint(_st.padding.left(), _st.padding.top());
	QRect r = App::app() ? App::app()->desktop()->screenGeometry(p) : QDesktopWidget().screenGeometry(p);
	if (rtl()) {
		if (w.x() - width() + 2 * _st.padding.left() < r.x() - _st.padding.left()) {
			w.setX(r.x() - _st.padding.left());
		} else {
			w.setX(w.x() - width() + 2 * _st.padding.left());
		}
	} else if (w.x() + width() - _st.padding.right() > r.x() + r.width()) {
		w.setX(r.x() + r.width() - width() + _st.padding.right());
	}
	if (w.y() + height() - _st.padding.bottom() > r.y() + r.height()) {
		w.setY(p.y() - height() + _st.padding.bottom());
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);
	showStart();
}

ContextMenu::~ContextMenu() {
	clearActions();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
    if (App::wnd()) {
        App::wnd()->activateWindow();
    }
#endif
}

PopupMenu::PopupMenu(const style::PopupMenu &st) : TWidget(0)
, _st(st)
, _itemHeight(_st.itemPadding.top() + _st.itemFont->height + _st.itemPadding.bottom())
, _mouseSelection(false)
, _shadow(_st.shadow)
, _selected(-1)
, a_opacity(1)
, _a_hide(animFunc(this, &PopupMenu::animStep_hide))
, _deleteOnHide(false) {
	_padding = _shadow.getDimensions(_st.shadowShift);

	resetActions();

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

QAction *PopupMenu::addAction(const QString &text, const QObject *receiver, const char* member) {
	QAction *a = 0;
	_actions.push_back(a = new QAction(text, this));
	connect(a, SIGNAL(triggered(bool)), this, SLOT(hideStart()));
	connect(a, SIGNAL(triggered(bool)), receiver, member);
	connect(a, SIGNAL(changed()), this, SLOT(actionChanged()));

	int32 w = _padding.left() + _st.widthMin + _padding.right();
	int32 mw = _padding.left() + _st.widthMax + _padding.right();
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		int32 goodw = _padding.left() + _st.itemPadding.left() + _st.itemFont->width(_actions[i]->text()) + _st.itemPadding.right() + _padding.right();
		w = snap(goodw, w, mw);
	}
	resize(w, height() + _itemHeight);
	update();

	return a;
}

PopupMenu::Actions &PopupMenu::actions() {
	return _actions;
}

void PopupMenu::actionChanged() {
	int32 w = _padding.left() + _st.widthMin + _padding.right();
	int32 mw = _padding.left() + _st.widthMax + _padding.right();
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		int32 goodw = _padding.left() + _st.itemPadding.left() + _st.itemFont->width(_actions[i]->text()) + _st.itemPadding.right() + _padding.right();
		w = snap(goodw, w, mw);
	}
	if (w != width()) {
		resize(w, height());
	}
	update();
}

void PopupMenu::activeWindowChanged() {
	if (!windowHandle()->isActive()) {
		hideStart();
	}
}

void PopupMenu::resetActions() {
	clearActions();
	resize(_padding.left() + _st.widthMin + _padding.right(), _padding.top() + (_st.skip * 2) + _padding.bottom());
}

void PopupMenu::clearActions() {
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		delete _actions[i];
	}
	_actions.clear();

	_selected = -1;
}

void PopupMenu::resizeEvent(QResizeEvent *e) {
	_inner = QRect(_padding.left(), _padding.top(), width() - _padding.left() - _padding.right(), height() - _padding.top() - _padding.bottom());
}

void PopupMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setClipRect(e->rect());
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	if (_a_hide.animating()) {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
		return;
	}

	p.fillRect(e->rect(), st::almostTransparent->b);
	p.setCompositionMode(m);

	_shadow.paint(p, _inner, _st.shadowShift);

	QRect topskip(_padding.left(), _padding.top(), _inner.width(), _st.skip);
	QRect bottomskip(_padding.left(), height() - _padding.bottom() - _st.skip, _inner.width(), _st.skip);
	if (e->rect().intersects(topskip)) p.fillRect(e->rect().intersected(topskip), _st.itemBg->b);
	if (e->rect().intersects(bottomskip)) p.fillRect(e->rect().intersected(bottomskip), _st.itemBg->b);

	int32 from = floorclamp(e->rect().top() - _padding.top() - _st.skip, _itemHeight, 0, _actions.size());
	int32 to = ceilclamp(e->rect().top() + e->rect().height() - _padding.top() - _st.skip, _itemHeight, 0, _actions.size());

	p.translate(_padding.left(), _padding.top() + _st.skip + (from * _itemHeight));
	p.setFont(_st.itemFont);
	for (int32 i = from; i < to; ++i) {
		p.fillRect(0, 0, _inner.width(), _itemHeight, (i == _selected ? _st.itemBgOver : _st.itemBg)->b);
		p.setPen(i == _selected ? _st.itemFgOver : _st.itemFg);
		p.drawTextLeft(_st.itemPadding.left(), _st.itemPadding.top(), width() - _padding.left() - _padding.right(), _actions.at(i)->text());
		p.translate(0, _itemHeight);
	}
}

void PopupMenu::updateSelected() {
	if (!_mouseSelection) return;

	QPoint p(mapFromGlobal(_mouse) - QPoint(_padding.left(), _padding.top() + _st.skip));
	setSelected(p.y() >= 0 ? (p.y() / _itemHeight) : -1);
}

void PopupMenu::itemPressed() {
	if (_selected >= 0 && _selected < _actions.size()) {
		emit _actions[_selected]->trigger();
		return;
	}
}

void PopupMenu::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		itemPressed();
	} else if (e->key() == Qt::Key_Escape) {
		hideStart();
		return;
	}
	if ((e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) || _actions.size() < 1) return;

	int32 newSelected = _selected + (e->key() == Qt::Key_Down ? 1 : -1);
	if (_selected < 0 || _selected >= _actions.size()) {
		newSelected = (e->key() == Qt::Key_Down) ? 0 : (_actions.size() - 1);
	} else {
		if (newSelected < 0) {
			newSelected = _actions.size() - 1;
		} else if (newSelected >= _actions.size()) {
			newSelected = 0;
		}
	}
	_mouseSelection = false;
	setSelected(newSelected);
}

void PopupMenu::enterEvent(QEvent *e) {
	QPoint mouse = QCursor::pos();
	if (_inner.contains(mapFromGlobal(mouse))) {
		_mouseSelection = true;
		_mouse = mouse;
		updateSelected();
	} else {
		_mouseSelection = false;
		setSelected(-1);
	}
}

void PopupMenu::leaveEvent(QEvent *e) {
	if (_mouseSelection) {
		_mouseSelection = false;
		setSelected(-1);
	}
}

void PopupMenu::setSelected(int32 newSelected) {
	if (newSelected >= _actions.size()) {
		newSelected = -1;
	}
	if (newSelected != _selected) {
		updateSelectedItem();
		_selected = newSelected;
		updateSelectedItem();
	}
}

void PopupMenu::updateSelectedItem() {
	if (_selected >= 0) {
		update(_padding.left(), _padding.top() + _st.skip + (_selected * _itemHeight), width() - _padding.left() - _padding.right(), _itemHeight);
	}
}

void PopupMenu::mouseMoveEvent(QMouseEvent *e) {
	if (_inner.contains(e->pos())) {
		_mouseSelection = true;
		_mouse = e->globalPos();
		updateSelected();
	} else {
		_mouseSelection = false;
		setSelected(-1);
	}
}

void PopupMenu::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	itemPressed();
}

void PopupMenu::focusOutEvent(QFocusEvent *e) {
	if (!_a_hide.animating()) hideStart();
}

void PopupMenu::fastHide() {
	if (_a_hide.animating()) {
		_a_hide.stop();
	}
	a_opacity = anim::fvalue(0, 0);
	hideFinish();
}

void PopupMenu::hideStart() {
	if (isHidden()) return;

	_cache = myGrab(this);
	a_opacity.start(0);
	_a_hide.start();
}

void PopupMenu::hideFinish() {
	hide();
	if (_deleteOnHide) {
		deleteLater();
	}
}

bool PopupMenu::animStep_hide(float64 ms) {
	float64 dt = ms / _st.duration;
	bool res = true;
	if (dt >= 1) {
		a_opacity.finish();
		hideFinish();
		res = false;
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
	return res;
}

void PopupMenu::deleteOnHide() {
	_deleteOnHide = true;
}

void PopupMenu::popup(const QPoint &p) {
	QPoint w = p - QPoint(0, _padding.top());
	QRect r = App::app() ? App::app()->desktop()->screenGeometry(p) : QDesktopWidget().screenGeometry(p);
	if (rtl()) {
		if (w.x() - width() < r.x() - _padding.left()) {
			w.setX(r.x() - _padding.left());
		} else {
			w.setX(w.x() - width());
		}
	} else if (w.x() + width() - _padding.right() > r.x() + r.width()) {
		w.setX(r.x() + r.width() - width() + _padding.right());
	}
	if (w.y() + height() - _padding.bottom() > r.y() + r.height()) {
		w.setY(p.y() - height() + _padding.bottom());
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);
	psUpdateOverlayed(this);
	show();
	psShowOverAll(this);
	windowHandle()->requestActivate();
	activateWindow();
	setFocus();
}

PopupMenu::~PopupMenu() {
	clearActions();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (App::wnd()) {
		App::wnd()->activateWindow();
	}
#endif
}
