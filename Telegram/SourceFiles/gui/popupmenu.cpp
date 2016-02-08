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
#include "stdafx.h"

#include "popupmenu.h"
#include "flatbutton.h"
#include "pspecific.h"

#include "application.h"

#include "lang.h"

PopupMenu::PopupMenu(const style::PopupMenu &st) : TWidget(0)
, _st(st)
, _menu(0)
, _parent(0)
, _itemHeight(_st.itemPadding.top() + _st.itemFont->height + _st.itemPadding.bottom())
, _separatorHeight(_st.separatorPadding.top() + _st.separatorWidth + _st.separatorPadding.bottom())
, _mouseSelection(false)
, _shadow(_st.shadow)
, _selected(-1)
, _childMenuIndex(-1)
, a_opacity(1)
, _a_hide(animation(this, &PopupMenu::step_hide))
, _deleteOnHide(true)
, _triggering(false)
, _deleteLater(false) {
	init();
}

PopupMenu::PopupMenu(QMenu *menu, const style::PopupMenu &st) : TWidget(0)
, _st(st)
, _menu(menu)
, _parent(0)
, _itemHeight(_st.itemPadding.top() + _st.itemFont->height + _st.itemPadding.bottom())
, _separatorHeight(_st.separatorPadding.top() + _st.separatorWidth + _st.separatorPadding.bottom())
, _mouseSelection(false)
, _shadow(_st.shadow)
, _selected(-1)
, _childMenuIndex(-1)
, a_opacity(1)
, _a_hide(animation(this, &PopupMenu::step_hide))
, _deleteOnHide(true)
, _triggering(false)
, _deleteLater(false) {
	init();
	QList<QAction*> actions(menu->actions());
	for (int32 i = 0, l = actions.size(); i < l; ++i) {
		addAction(actions.at(i));
	}
}

void PopupMenu::init() {
	_padding = _shadow.getDimensions(_st.shadowShift);

	resetActions();

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Popup | Qt::NoDropShadowWindowHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

QAction *PopupMenu::addAction(const QString &text, const QObject *receiver, const char* member) {
	QAction *a = new QAction(text, this);
	connect(a, SIGNAL(triggered(bool)), receiver, member);
	return addAction(a);
}

QAction *PopupMenu::addAction(QAction *a) {
	connect(a, SIGNAL(changed()), this, SLOT(actionChanged()));
	_actions.push_back(a);
	if (a->menu()) {
		_menus.push_back(new PopupMenu(a->menu()));
		_menus.back()->deleteOnHide(false);
	} else {
		_menus.push_back(0);
	}
	_texts.push_back(QString());
	_shortcutTexts.push_back(QString());
	int32 w = processAction(a, _actions.size() - 1, width());
	resize(w, height() + (a->isSeparator() ? _separatorHeight : _itemHeight));
	update();

	return a;
}

int32 PopupMenu::processAction(QAction *a, int32 index, int32 w) {
	if (a->isSeparator() || a->text().isEmpty()) {
		_texts[index] = _shortcutTexts[index] = QString();
	} else {
		QStringList texts = a->text().split('\t');
		int32 textw = _st.itemFont->width(texts.at(0));
		int32 goodw = _padding.left() + _st.itemPadding.left() + textw + _st.itemPadding.right() + _padding.right();
		if (_menus.at(index)) {
			goodw += _st.itemPadding.left() + _st.arrow.pxWidth();
		} else if (texts.size() > 1) {
			goodw += _st.itemPadding.left() + _st.itemFont->width(texts.at(1));
		}
		w = snap(goodw, w, int32(_padding.left() + _st.widthMax + _padding.right()));
		_texts[index] = (w < goodw) ? _st.itemFont->elided(texts.at(0), w - (goodw - textw)) : texts.at(0);
		_shortcutTexts[index] = texts.size() > 1 ? texts.at(1) : QString();
	}
	return w;
}

PopupMenu::Actions &PopupMenu::actions() {
	return _actions;
}

void PopupMenu::actionChanged() {
	int32 w = _padding.left() + _st.widthMin + _padding.right();
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		w = processAction(_actions.at(i), i, w);
	}
	if (w != width()) {
		resize(w, height());
	}
	update();
}

void PopupMenu::resetActions() {
	clearActions();
	resize(_padding.left() + _st.widthMin + _padding.right(), _padding.top() + (_st.skip * 2) + _padding.bottom());
}

void PopupMenu::clearActions(bool force) {
	if (_menu && !force) return;

	if (!_menu) {
		for (int32 i = 0, l = _actions.size(); i < l; ++i) {
			delete _actions[i];
		}
	}
	_actions.clear();

	for (int32 i = 0, l = _menus.size(); i < l; ++i) {
		delete _menus[i];
	}
	_menus.clear();
	_childMenuIndex = -1;

	_selected = -1;
}

void PopupMenu::resizeEvent(QResizeEvent *e) {
	_inner = QRect(_padding.left(), _padding.top(), width() - _padding.left() - _padding.right(), height() - _padding.top() - _padding.bottom());
	return TWidget::resizeEvent(e);
}

void PopupMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	p.setClipRect(r);
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	if (_a_hide.animating()) {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
		return;
	}

	p.fillRect(r, st::almostTransparent->b);
	p.setCompositionMode(m);

	_shadow.paint(p, _inner, _st.shadowShift);

	QRect topskip(_padding.left(), _padding.top(), _inner.width(), _st.skip);
	QRect bottomskip(_padding.left(), height() - _padding.bottom() - _st.skip, _inner.width(), _st.skip);
	if (r.intersects(topskip)) p.fillRect(r.intersected(topskip), _st.itemBg->b);
	if (r.intersects(bottomskip)) p.fillRect(r.intersected(bottomskip), _st.itemBg->b);

	int32 y = _padding.top() + _st.skip;
	p.translate(_padding.left(), y);
	p.setFont(_st.itemFont);
	for (int32 i = 0, l = _actions.size(); i < l; ++i) {
		if (r.top() + r.height() <= y) break;
		int32 h = _actions.at(i)->isSeparator() ? _separatorHeight : _itemHeight;
		y += h;
		if (r.top() < y) {
			if (_actions.at(i)->isSeparator()) {
				p.fillRect(0, 0, _inner.width(), h, _st.itemBg->b);
				p.fillRect(_st.separatorPadding.left(), _st.separatorPadding.top(), _inner.width() - _st.separatorPadding.left() - _st.separatorPadding.right(), _st.separatorWidth, _st.separatorFg->b);
			} else {
				bool enabled = _actions.at(i)->isEnabled(), selected = (i == _selected && enabled);
				p.fillRect(0, 0, _inner.width(), h, (selected ? _st.itemBgOver : _st.itemBg)->b);
				p.setPen(selected ? _st.itemFgOver : (enabled ? _st.itemFg : _st.itemFgDisabled));
				p.drawTextLeft(_st.itemPadding.left(), _st.itemPadding.top(), _inner.width(), _texts.at(i));
				if (_menus.at(i)) {
					p.drawSpriteRight(_st.itemPadding.right(), (_itemHeight - _st.arrow.pxHeight()) / 2, _inner.width(), _st.arrow);
				} else if (!_shortcutTexts.at(i).isEmpty()) {
					p.setPen(selected ? _st.itemFgShortcutOver : (enabled ? _st.itemFgShortcut : _st.itemFgShortcutDisabled));
					p.drawTextRight(_st.itemPadding.right(), _st.itemPadding.top(), _inner.width(), _shortcutTexts.at(i));
				}
			}
		}
		p.translate(0, h);
	}
}

void PopupMenu::updateSelected() {
	if (!_mouseSelection) return;

	QPoint p(mapFromGlobal(_mouse) - QPoint(_padding.left(), _padding.top() + _st.skip));
	int32 selected = -1, y = 0;
	while (y <= p.y() && ++selected < _actions.size()) {
		y += _actions.at(selected)->isSeparator() ? _separatorHeight : _itemHeight;
	}
	setSelected((selected >= 0 && selected < _actions.size() && _actions.at(selected)->isEnabled() && !_actions.at(selected)->isSeparator()) ? selected : -1);
}

void PopupMenu::itemPressed(PressSource source) {
	if (source == PressSourceMouse && !_mouseSelection) {
		return;
	}
	if (_selected >= 0 && _selected < _actions.size() && _actions[_selected]->isEnabled()) {
		if (_menus.at(_selected)) {
			if (_childMenuIndex == _selected) {
				_menus.at(_childMenuIndex)->hideMenu(true);
			} else {
				popupChildMenu(source);
			}
		} else {
			hideMenu();
			_triggering = true;
			emit _actions[_selected]->trigger();
			_triggering = false;
			if (_deleteLater) {
				_deleteLater = false;
				deleteLater();
			}
		}
	}
}

void PopupMenu::popupChildMenu(PressSource source) {
	if (_childMenuIndex >= 0) {
		_menus.at(_childMenuIndex)->hideMenu(true);
		_childMenuIndex = -1;
	}
	if (_selected >= 0 && _selected < _menus.size() && _menus.at(_selected)) {
		QPoint p(_inner.x() + (rtl() ? _padding.right() : _inner.width() - _padding.left()), _inner.y() + _st.skip + itemY(_selected));
		_childMenuIndex = _selected;
		_menus.at(_childMenuIndex)->showMenu(geometry().topLeft() + p, this, source);
	}
}

void PopupMenu::keyPressEvent(QKeyEvent *e) {
	if (_childMenuIndex >= 0) {
		return _menus.at(_childMenuIndex)->keyPressEvent(e);
	}

	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		itemPressed(PressSourceKeyboard);
		return;
	} else if (e->key() == Qt::Key_Escape) {
		hideMenu(_parent ? true : false);
		return;
	}
	if (e->key() == (rtl() ? Qt::Key_Left : Qt::Key_Right)) {
		if (_selected >= 0 && _menus.at(_selected)) {
			itemPressed(PressSourceKeyboard);
			return;
		} else if (_selected < 0 && _parent && !_actions.isEmpty()) {
			_mouseSelection = false;
			setSelected(0);
		}
	} else if (e->key() == (rtl() ? Qt::Key_Right : Qt::Key_Left)) {
		if (_parent) {
			hideMenu(true);
		}
	}
	if ((e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) || _actions.size() < 1) return;

	int32 delta = (e->key() == Qt::Key_Down ? 1 : -1), start = _selected;
	if (start < 0 || start >= _actions.size()) {
		start = (delta > 0) ? (_actions.size() - 1) : 0;
	}
	int32 newSelected = start;
	do {
		newSelected += delta;
		if (newSelected < 0) {
			newSelected += _actions.size();
		} else if (newSelected >= _actions.size()) {
			newSelected -= _actions.size();
		}
	} while (newSelected != start && (!_actions.at(newSelected)->isEnabled() || _actions.at(newSelected)->isSeparator()));

	if (_actions.at(newSelected)->isEnabled() && !_actions.at(newSelected)->isSeparator()) {
		_mouseSelection = false;
		setSelected(newSelected);
	}
}

void PopupMenu::enterEvent(QEvent *e) {
	QPoint mouse = QCursor::pos();
	if (!_inner.marginsRemoved(QMargins(0, _st.skip, 0, _st.skip)).contains(mapFromGlobal(mouse))) {
		if (_mouseSelection && _childMenuIndex < 0) {
			_mouseSelection = false;
			setSelected(-1);
		}
	}
	return TWidget::enterEvent(e);
}

void PopupMenu::leaveEvent(QEvent *e) {
	if (_mouseSelection && _childMenuIndex < 0) {
		_mouseSelection = false;
		setSelected(-1);
	}
	return TWidget::leaveEvent(e);
}

void PopupMenu::setSelected(int32 newSelected) {
	if (newSelected >= _actions.size()) {
		newSelected = -1;
	}
	if (newSelected != _selected) {
		updateSelectedItem();
		_selected = newSelected;
		if (_mouseSelection) {
			popupChildMenu(PressSourceMouse);
		}
		updateSelectedItem();
	}
}

int32 PopupMenu::itemY(int32 index) {
	if (index > _actions.size()) {
		index = _actions.size();
	}
	int32 y = 0;
	for (int32 i = 0; i < index; ++i) {
		y += _actions.at(i)->isSeparator() ? _separatorHeight : _itemHeight;
	}
	return y;
}

void PopupMenu::updateSelectedItem() {
	if (_selected >= 0) {
		update(_padding.left(), _padding.top() + _st.skip + itemY(_selected), width() - _padding.left() - _padding.right(), _actions.at(_selected)->isSeparator() ? _separatorHeight : _itemHeight);
	}
}

void PopupMenu::mouseMoveEvent(QMouseEvent *e) {
	if (_inner.marginsRemoved(QMargins(0, _st.skip, 0, _st.skip)).contains(mapFromGlobal(e->globalPos()))) {
		_mouseSelection = true;
		_mouse = e->globalPos();
		updateSelected();
	} else {
		if (_mouseSelection && _childMenuIndex < 0) {
			_mouseSelection = false;
			setSelected(-1);
		}
		if (_parent) {
			_parent->mouseMoveEvent(e);
		}
	}
}

void PopupMenu::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_inner.contains(mapFromGlobal(e->globalPos()))) {
		itemPressed(PressSourceMouse);
		return;
	}
	if (_parent) {
		_parent->mousePressEvent(e);
	} else {
		hideMenu();
	}
}

void PopupMenu::focusOutEvent(QFocusEvent *e) {
	hideMenu();
}

void PopupMenu::hideEvent(QHideEvent *e) {
	if (_deleteOnHide) {
		if (_triggering) {
			_deleteLater = true;
		} else {
			deleteLater();
		}
	}
}

void PopupMenu::hideMenu(bool fast) {
	if (isHidden()) return;
	if (_parent && !_a_hide.animating()) {
		_parent->childHiding(this);
	}
	if (fast) {
		if (_a_hide.animating()) {
			_a_hide.stop();
		}
		a_opacity = anim::fvalue(0, 0);
		hideFinish();
	} else {
		if (!_a_hide.animating()) {
			_cache = myGrab(this);
			a_opacity.start(0);
			_a_hide.start();
		}
		if (_parent) {
			_parent->hideMenu();
		}
	}
	if (_childMenuIndex >= 0) {
		_menus.at(_childMenuIndex)->hideMenu(fast);
	}
}

void PopupMenu::childHiding(PopupMenu *child) {
	if (_childMenuIndex >= 0 && _menus.at(_childMenuIndex) == child) {
		_childMenuIndex = -1;
	}
}

void PopupMenu::hideFinish() {
	hide();
}

void PopupMenu::step_hide(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_hide.stop();
		a_opacity.finish();
		hideFinish();
	} else {
		a_opacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void PopupMenu::deleteOnHide(bool del) {
	_deleteOnHide = del;
}

void PopupMenu::popup(const QPoint &p) {
	showMenu(p, 0, PressSourceMouse);
}

void PopupMenu::showMenu(const QPoint &p, PopupMenu *parent, PressSource source) {
	_parent = parent;

	QPoint w = p - QPoint(0, _padding.top());
	QRect r = Sandbox::screenGeometry(p);
	if (rtl()) {
		if (w.x() - width() < r.x() - _padding.left()) {
			if (_parent && w.x() + _parent->width() - _padding.left() - _padding.right() + width() - _padding.right() <= r.x() + r.width()) {
				w.setX(w.x() + _parent->width() - _padding.left() - _padding.right());
			} else {
				w.setX(r.x() - _padding.left());
			}
		} else {
			w.setX(w.x() - width());
		}
	} else {
		if (w.x() + width() - _padding.right() > r.x() + r.width()) {
			if (_parent && w.x() - _parent->width() + _padding.left() + _padding.right() - width() + _padding.right() >= r.x() - _padding.left()) {
				w.setX(w.x() + _padding.left() + _padding.right() - _parent->width() - width() + _padding.left() + _padding.right());
			} else {
				w.setX(r.x() + r.width() - width() + _padding.right());
			}
		}
	}
	if (w.y() + height() - _padding.bottom() > r.y() + r.height()) {
		if (_parent) {
			w.setY(r.y() + r.height() - height() + _padding.bottom());
		} else {
			w.setY(p.y() - height() + _padding.bottom());
		}
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);

	_mouseSelection = (source == PressSourceMouse);
	setSelected((source == PressSourceMouse || _actions.isEmpty()) ? -1 : 0);
	psUpdateOverlayed(this);
	show();
	psShowOverAll(this);
	windowHandle()->requestActivate();
	activateWindow();

	if (_a_hide.animating()) {
		_a_hide.stop();
		_cache = QPixmap();
	}
	a_opacity = anim::fvalue(1, 1);
}

PopupMenu::~PopupMenu() {
	clearActions(true);
	delete _menu;
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (App::wnd()) {
		App::wnd()->activateWindow();
	}
#endif
}

PopupTooltip *PopupTooltipInstance = 0;

AbstractTooltipShower::~AbstractTooltipShower() {
	if (PopupTooltipInstance && PopupTooltipInstance->_shower == this) {
		PopupTooltipInstance->_shower = 0;
	}
}

PopupTooltip::PopupTooltip() : TWidget(0)
, _shower(0)
, _st(0) {
	PopupTooltipInstance = this;

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::ToolTip | Qt::NoDropShadowWindowHint);
	setAttribute(Qt::WA_NoSystemBackground, true);

	_showTimer.setSingleShot(true);
	connect(&_showTimer, SIGNAL(timeout()), this, SLOT(onShow()));
}

void PopupTooltip::onShow() {
	if (_shower) {
		QString text = _shower->tooltipText();
		if (text.isEmpty()) {
			Hide();
		} else {
			PopupTooltipInstance->popup(_shower->tooltipPos(), text, _shower->tooltipSt());
		}
	}
}

bool PopupTooltip::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Leave) {
		_hideByLeaveTimer.start(10);
	} else if (e->type() == QEvent::Enter) {
		_hideByLeaveTimer.stop();
	} else if (e->type() == QEvent::MouseMove) {
		if ((QCursor::pos() - _point).manhattanLength() > QApplication::startDragDistance()) {
			Hide();
		}
	}
	return TWidget::eventFilter(o, e);
}

void PopupTooltip::onHideByLeave() {
	Hide();
}

PopupTooltip::~PopupTooltip() {
	if (PopupTooltipInstance == this) {
		PopupTooltipInstance = 0;
	}
}

void PopupTooltip::popup(const QPoint &m, const QString &text, const style::Tooltip *st) {
	if (!_hideByLeaveTimer.isSingleShot()) {
		_hideByLeaveTimer.setSingleShot(true);
		connect(&_hideByLeaveTimer, SIGNAL(timeout()), this, SLOT(onHideByLeave()));

		Sandbox::installEventFilter(this);
	}

	_point = m;
	_st = st;
	_text = Text(_st->textFont, text, _textPlainOptions, _st->widthMax, true);

	int32 addw = 2 * st::lineWidth + _st->textPadding.left() + _st->textPadding.right();
	int32 addh = 2 * st::lineWidth + _st->textPadding.top() + _st->textPadding.bottom();

	// count tooltip size
	QSize s(addw + _text.maxWidth(), addh + _text.minHeight());
	if (s.width() > _st->widthMax) {
		s.setWidth(addw + _text.countWidth(_st->widthMax - addw));
		s.setHeight(addh + _text.countHeight(s.width() - addw));
	}
	int32 maxh = addh + (_st->linesMax * _st->textFont->height);
	if (s.height() > maxh) {
		s.setHeight(maxh);
	}

	// count tooltip position
	QPoint p(m + _st->shift);
	if (rtl()) {
		p.setX(m.x() - s.width() - _st->shift.x());
	}
	if (s.width() < 2 * _st->shift.x()) {
		p.setX(m.x() - (s.width() / 2));
	}

	// adjust tooltip position
	QRect r(QApplication::desktop()->screenGeometry(m));
	if (r.x() + r.width() - _st->skip < p.x() + s.width() && p.x() + s.width() > m.x()) {
		p.setX(qMax(r.x() + r.width() - int32(_st->skip) - s.width(), m.x() - s.width()));
	}
	if (r.x() + _st->skip > p.x() && p.x() < m.x()) {
		p.setX(qMin(m.x(), r.x() + int32(_st->skip)));
	}
	if (r.y() + r.height() - _st->skip < p.y() + s.height()) {
		p.setY(m.y() - s.height() - _st->skip);
	}
	if (r.y() > p.x()) {
		p.setY(qMin(m.y() + _st->shift.y(), r.y() + r.height() - s.height()));
	}

	setGeometry(QRect(p, s));

	_hideByLeaveTimer.stop();
	show();
}

void PopupTooltip::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(rect(), _st->textBg);

	p.fillRect(QRect(0, 0, width(), st::lineWidth), _st->textBorder);
	p.fillRect(QRect(0, height() - st::lineWidth, width(), st::lineWidth), _st->textBorder);
	p.fillRect(QRect(0, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);
	p.fillRect(QRect(width() - st::lineWidth, st::lineWidth, st::lineWidth, height() - 2 * st::lineWidth), _st->textBorder);

	int32 lines = qFloor((height() - 2 * st::lineWidth - _st->textPadding.top() - _st->textPadding.bottom()) / _st->textFont->height);

	p.setPen(_st->textFg);
	_text.drawElided(p, st::lineWidth + _st->textPadding.left(), st::lineWidth + _st->textPadding.top(), width() - 2 * st::lineWidth - _st->textPadding.left() - _st->textPadding.right(), lines);
}

void PopupTooltip::hideEvent(QHideEvent *e) {
	if (PopupTooltipInstance == this) {
		Hide();
	}
}

void PopupTooltip::Show(int32 delay, const AbstractTooltipShower *shower) {
	if (!PopupTooltipInstance) {
		new PopupTooltip();
	}
	PopupTooltipInstance->_shower = shower;
	if (delay >= 0) {
		PopupTooltipInstance->_showTimer.start(delay);
	} else {
		PopupTooltipInstance->onShow();
	}
}

void PopupTooltip::Hide() {
	if (PopupTooltip *instance = PopupTooltipInstance) {
		PopupTooltipInstance = 0;
		instance->_showTimer.stop();
		instance->_hideByLeaveTimer.stop();
		instance->hide();
		instance->deleteLater();
	}
}
