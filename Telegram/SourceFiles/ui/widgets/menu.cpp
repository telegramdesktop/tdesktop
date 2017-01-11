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
#include "stdafx.h"
#include "ui/widgets/menu.h"

#include "ui/effects/ripple_animation.h"

namespace Ui {

Menu::Menu(QWidget *parent, const style::Menu &st) : TWidget(parent)
, _st(st)
, _itemHeight(_st.itemPadding.top() + _st.itemFont->height + _st.itemPadding.bottom())
, _separatorHeight(_st.separatorPadding.top() + _st.separatorWidth + _st.separatorPadding.bottom()) {
	init();
}

Menu::Menu(QWidget *parent, QMenu *menu, const style::Menu &st) : TWidget(parent)
, _st(st)
, _wappedMenu(menu)
, _itemHeight(_st.itemPadding.top() + _st.itemFont->height + _st.itemPadding.bottom())
, _separatorHeight(_st.separatorPadding.top() + _st.separatorWidth + _st.separatorPadding.bottom()) {
	init();

	_wappedMenu->setParent(this);
	for (auto action : _wappedMenu->actions()) {
		addAction(action);
	}
	_wappedMenu->hide();
}

void Menu::init() {
	resize(_st.widthMin, _st.skip * 2);

	setMouseTracking(true);

	setAttribute(Qt::WA_OpaquePaintEvent);
}

QAction *Menu::addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon, const style::icon *iconOver) {
	auto action = addAction(new QAction(text, this), icon, iconOver);
	connect(action, SIGNAL(triggered(bool)), receiver, member, Qt::QueuedConnection);
	return action;
}

QAction *Menu::addAction(const QString &text, base::lambda<void()> &&callback, const style::icon *icon, const style::icon *iconOver) {
	auto action = addAction(new QAction(text, this), icon, iconOver);
	connect(action, SIGNAL(triggered(bool)), base::lambda_slot(action, std_::move(callback)), SLOT(action()), Qt::QueuedConnection);
	return action;
}

QAction *Menu::addAction(QAction *action, const style::icon *icon, const style::icon *iconOver) {
	connect(action, SIGNAL(changed()), this, SLOT(actionChanged()));
	_actions.push_back(action);

	ActionData data;
	data.icon = icon;
	data.iconOver = iconOver ? iconOver : icon;
	data.hasSubmenu = (action->menu() != nullptr);
	_actionsData.push_back(data);

	auto newWidth = qMax(width(), _st.widthMin);
	newWidth = processAction(action, _actions.size() - 1, newWidth);
	auto newHeight = height() + (action->isSeparator() ? _separatorHeight : _itemHeight);
	resize(newWidth, newHeight);
	if (_resizedCallback) {
		_resizedCallback();
	}
	update();

	return action;
}

QAction *Menu::addSeparator() {
	auto separator = new QAction(this);
	separator->setSeparator(true);
	return addAction(separator);
}

void Menu::clearActions() {
	_actionsData.clear();
	for (auto action : base::take(_actions)) {
		if (action->parent() == this) {
			delete action;
		}
	}
	resize(_st.widthMin, _st.skip * 2);
	if (_resizedCallback) {
		_resizedCallback();
	}
}

int Menu::processAction(QAction *action, int index, int width) {
	auto &data = _actionsData[index];
	if (action->isSeparator() || action->text().isEmpty()) {
		data.text = data.shortcut = QString();
	} else {
		auto actionTextParts = action->text().split('\t');
		auto actionText = actionTextParts.empty() ? QString() : actionTextParts[0];
		auto actionShortcut = (actionTextParts.size() > 1) ? actionTextParts[1] : QString();
		int textw = _st.itemFont->width(actionText);
		int goodw = _st.itemPadding.left() + textw + _st.itemPadding.right();
		if (data.hasSubmenu) {
			goodw += _st.itemPadding.left() + _st.arrow.width();
		} else if (!actionShortcut.isEmpty()) {
			goodw += _st.itemPadding.left() + _st.itemFont->width(actionShortcut);
		}
		width = snap(goodw, width, _st.widthMax);
		data.text = (width < goodw) ? _st.itemFont->elided(actionText, width - (goodw - textw)) : actionText;
		data.shortcut = actionShortcut;
	}
	return width;
}

void Menu::setShowSource(TriggeredSource source) {
	_mouseSelection = (source == TriggeredSource::Mouse);
	setSelected((source == TriggeredSource::Mouse || _actions.isEmpty()) ? -1 : 0);
}

Menu::Actions &Menu::actions() {
	return _actions;
}

void Menu::actionChanged() {
	int newWidth = _st.widthMin;
	for (int i = 0, count = _actions.size(); i != count; ++i) {
		newWidth = processAction(_actions[i], i, newWidth);
	}
	if (newWidth != width()) {
		resize(newWidth, height());
		if (_resizedCallback) {
			_resizedCallback();
		}
	}
	update();
}

void Menu::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	auto topskip = QRect(0, 0, width(), _st.skip);
	auto bottomskip = QRect(0, height() - _st.skip, width(), _st.skip);
	if (clip.intersects(topskip)) p.fillRect(clip.intersected(topskip), _st.itemBg);
	if (clip.intersects(bottomskip)) p.fillRect(clip.intersected(bottomskip), _st.itemBg);

	int top = _st.skip;
	p.translate(0, top);
	p.setFont(_st.itemFont);
	for (int i = 0, count = _actions.size(); i != count; ++i) {
		if (clip.top() + clip.height() <= top) break;

		auto action = _actions[i];
		auto &data = _actionsData[i];
		auto actionHeight = action->isSeparator() ? _separatorHeight : _itemHeight;
		top += actionHeight;
		if (clip.top() < top) {
			if (action->isSeparator()) {
				p.fillRect(0, 0, width(), actionHeight, _st.itemBg);
				p.fillRect(_st.separatorPadding.left(), _st.separatorPadding.top(), width() - _st.separatorPadding.left() - _st.separatorPadding.right(), _st.separatorWidth, _st.separatorFg);
			} else {
				auto enabled = action->isEnabled();
				auto selected = ((i == _selected || i == _pressed) && enabled);
				p.fillRect(0, 0, width(), actionHeight, selected ? _st.itemBgOver : _st.itemBg);
				if (data.ripple) {
					data.ripple->paint(p, 0, 0, width(), ms);
					if (data.ripple->empty()) {
						data.ripple.reset();
					}
				}
				if (auto icon = (selected ? data.iconOver : data.icon)) {
					icon->paint(p, _st.itemIconPosition, width());
				}
				p.setPen(selected ? _st.itemFgOver : (enabled ? _st.itemFg : _st.itemFgDisabled));
				p.drawTextLeft(_st.itemPadding.left(), _st.itemPadding.top(), width(), data.text);
				if (data.hasSubmenu) {
					_st.arrow.paint(p, width() - _st.itemPadding.right() - _st.arrow.width(), (_itemHeight - _st.arrow.height()) / 2, width());
				} else if (!data.shortcut.isEmpty()) {
					p.setPen(selected ? _st.itemFgShortcutOver : (enabled ? _st.itemFgShortcut : _st.itemFgShortcutDisabled));
					p.drawTextRight(_st.itemPadding.right(), _st.itemPadding.top(), width(), data.shortcut);
				}
			}
		}
		p.translate(0, actionHeight);
	}
}

void Menu::updateSelected(QPoint globalPosition) {
	if (!_mouseSelection) return;

	auto p = mapFromGlobal(globalPosition) - QPoint(0, _st.skip);
	auto selected = -1, top = 0;
	while (top <= p.y() && ++selected < _actions.size()) {
		top += _actions[selected]->isSeparator() ? _separatorHeight : _itemHeight;
	}
	setSelected((selected >= 0 && selected < _actions.size() && _actions[selected]->isEnabled() && !_actions[selected]->isSeparator()) ? selected : -1);
}

void Menu::itemPressed(TriggeredSource source) {
	if (source == TriggeredSource::Mouse && !_mouseSelection) {
		return;
	}
	if (_selected >= 0 && _selected < _actions.size() && _actions[_selected]->isEnabled()) {
		_pressed = _selected;
		if (source == TriggeredSource::Mouse) {
			if (!_actionsData[_pressed].ripple) {
				auto mask = RippleAnimation::rectMask(QSize(width(), _itemHeight));
				_actionsData[_pressed].ripple = MakeShared<RippleAnimation>(_st.ripple, std_::move(mask), [this, selected = _pressed] {
					updateItem(selected);
				});
			}
			_actionsData[_pressed].ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, itemTop(_pressed)));
		} else {
			itemReleased(source);
		}
	}
}

void Menu::itemReleased(TriggeredSource source) {
	auto pressed = base::take(_pressed, -1);
	if (pressed >= 0 && pressed < _actions.size()) {
		if (source == TriggeredSource::Mouse && _actionsData[pressed].ripple) {
			_actionsData[pressed].ripple->lastStop();
		}
		if (pressed == _selected && _triggeredCallback) {
			_triggeredCallback(_actions[_selected], itemTop(_selected), source);
		}
	}
}

void Menu::keyPressEvent(QKeyEvent *e) {
	auto key = e->key();
	if (!_keyPressDelegate || !_keyPressDelegate(key)) {
		handleKeyPress(key);
	}
}

void Menu::handleKeyPress(int key) {
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		itemPressed(TriggeredSource::Keyboard);
		return;
	}
	if (key == (rtl() ? Qt::Key_Left : Qt::Key_Right)) {
		if (_selected >= 0 && _actionsData[_selected].hasSubmenu) {
			itemPressed(TriggeredSource::Keyboard);
			return;
		} else if (_selected < 0 && !_actions.isEmpty()) {
			_mouseSelection = false;
			setSelected(0);
		}
	}
	if ((key != Qt::Key_Up && key != Qt::Key_Down) || _actions.size() < 1) return;

	auto delta = (key == Qt::Key_Down ? 1 : -1), start = _selected;
	if (start < 0 || start >= _actions.size()) {
		start = (delta > 0) ? (_actions.size() - 1) : 0;
	}
	auto newSelected = start;
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

void Menu::clearSelection() {
	_mouseSelection = false;
	setSelected(-1);
}

void Menu::clearMouseSelection() {
	if (_mouseSelection && !_childShown) {
		clearSelection();
	}
}

void Menu::enterEvent(QEvent *e) {
	QPoint mouse = QCursor::pos();
	if (!rect().marginsRemoved(QMargins(0, _st.skip, 0, _st.skip)).contains(mapFromGlobal(mouse))) {
		clearMouseSelection();
	}
	return TWidget::enterEvent(e);
}

void Menu::leaveEvent(QEvent *e) {
	clearMouseSelection();
	return TWidget::leaveEvent(e);
}

void Menu::setSelected(int selected) {
	if (selected >= _actions.size()) {
		selected = -1;
	}
	if (_selected != selected) {
		updateSelectedItem();
		_selected = selected;
		updateSelectedItem();
		if (_activatedCallback) {
			auto source = _mouseSelection ? TriggeredSource::Mouse : TriggeredSource::Keyboard;
			_activatedCallback((_selected >= 0) ? _actions[_selected] : nullptr, itemTop(_selected), source);
		}
	}
}

int Menu::itemTop(int index) {
	if (index > _actions.size()) {
		index = _actions.size();
	}
	int top = _st.skip;
	for (int i = 0; i < index; ++i) {
		top += _actions.at(i)->isSeparator() ? _separatorHeight : _itemHeight;
	}
	return top;
}

void Menu::updateItem(int index) {
	if (index >= 0 && index < _actions.size()) {
		update(0, itemTop(index), width(), _actions[index]->isSeparator() ? _separatorHeight : _itemHeight);
	}
}

void Menu::updateSelectedItem() {
	updateItem(_selected);
}

void Menu::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void Menu::handleMouseMove(QPoint globalPosition) {
	auto inner = rect().marginsRemoved(QMargins(0, _st.skip, 0, _st.skip));
	auto localPosition = mapFromGlobal(globalPosition);
	if (inner.contains(localPosition)) {
		_mouseSelection = true;
		updateSelected(globalPosition);
	} else {
		clearMouseSelection();
		if (_mouseMoveDelegate) {
			_mouseMoveDelegate(globalPosition);
		}
	}
}

void Menu::mousePressEvent(QMouseEvent *e) {
	handleMousePress(e->globalPos());
}

void Menu::mouseReleaseEvent(QMouseEvent *e) {
	handleMouseRelease(e->globalPos());
}

void Menu::handleMousePress(QPoint globalPosition) {
	handleMouseMove(globalPosition);
	if (rect().contains(mapFromGlobal(globalPosition))) {
		itemPressed(TriggeredSource::Mouse);
	} else if (_mousePressDelegate) {
		_mousePressDelegate(globalPosition);
	}
}

void Menu::handleMouseRelease(QPoint globalPosition) {
	handleMouseMove(globalPosition);
	itemReleased(TriggeredSource::Mouse);
	if (!rect().contains(mapFromGlobal(globalPosition)) && _mouseReleaseDelegate) {
		_mouseReleaseDelegate(globalPosition);
	}
}

} // namespace Ui
