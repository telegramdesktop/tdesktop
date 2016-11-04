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
#include "ui/widgets/popup_menu.h"

#include "pspecific.h"
#include "application.h"
#include "lang.h"

namespace Ui {

PopupMenu::PopupMenu(const style::PopupMenu &st) : TWidget(nullptr)
, _st(st)
, _menu(this, _st.menu)
, _shadow(_st.shadow)
, a_opacity(1)
, _a_hide(animation(this, &PopupMenu::step_hide)) {
	init();
}

PopupMenu::PopupMenu(QMenu *menu, const style::PopupMenu &st) : TWidget(nullptr)
, _st(st)
, _menu(this, menu, _st.menu)
, _shadow(_st.shadow)
, a_opacity(1)
, _a_hide(animation(this, &PopupMenu::step_hide)) {
	init();

	for (auto action : actions()) {
		if (auto submenu = action->menu()) {
			auto it = _submenus.insert(action, new PopupMenu(submenu, st));
			it.value()->deleteOnHide(false);
		}
	}
}

void PopupMenu::init() {
	_padding = _shadow.getDimensions(_st.shadowShift);

	_menu->setResizedCallback([this] { handleMenuResize(); });
	_menu->setActivatedCallback([this](QAction *action, int actionTop, TriggeredSource source) {
		handleActivated(action, actionTop, source);
	});
	_menu->setTriggeredCallback([this](QAction *action, int actionTop, TriggeredSource source) {
		handleTriggered(action, actionTop, source);
	});
	_menu->setKeyPressDelegate([this](int key) { return handleKeyPress(key); });
	_menu->setMouseMoveDelegate([this](QPoint globalPosition) { handleMouseMove(globalPosition); });
	_menu->setMousePressDelegate([this](QPoint globalPosition) { handleMousePress(globalPosition); });

	_menu->moveToLeft(_padding.left(), _padding.top());
	handleMenuResize();

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::BypassWindowManagerHint | Qt::Popup | Qt::NoDropShadowWindowHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

void PopupMenu::handleMenuResize() {
	resize(_padding.left() + _menu->width() + _padding.right(), _padding.top() + _menu->height() + _padding.bottom());
	_inner = QRect(_padding.left(), _padding.top(), width() - _padding.left() - _padding.right(), height() - _padding.top() - _padding.bottom());
}

QAction *PopupMenu::addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, receiver, member, icon, iconOver);
}

QAction *PopupMenu::addAction(const QString &text, base::lambda_unique<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std_::move(callback), icon, iconOver);
}

QAction *PopupMenu::addSeparator() {
	return _menu->addSeparator();
}

void PopupMenu::clearActions() {
	for (auto submenu : base::take(_submenus)) {
		delete submenu;
	}
	return _menu->clearActions();
}

PopupMenu::Actions &PopupMenu::actions() {
	return _menu->actions();
}

void PopupMenu::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	p.setClipRect(clip);
	auto compositionMode = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	if (_a_hide.animating()) {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
		return;
	}

	// This is the minimal alpha value that allowed mouse tracking in OS X.
	p.fillRect(clip, QColor(255, 255, 255, 13));
	p.setCompositionMode(compositionMode);

	_shadow.paint(p, _inner, _st.shadowShift);
}

void PopupMenu::handleActivated(QAction *action, int actionTop, TriggeredSource source) {
	if (source == TriggeredSource::Mouse) {
		if (!popupSubmenuFromAction(action, actionTop, source)) {
			if (auto currentSubmenu = base::take(_activeSubmenu)) {
				currentSubmenu->hideMenu(true);
			}
		}
	}
}

void PopupMenu::handleTriggered(QAction *action, int actionTop, TriggeredSource source) {
	if (!popupSubmenuFromAction(action, actionTop, source)) {
		hideMenu();
		_triggering = true;
		emit action->trigger();
		_triggering = false;
		if (_deleteLater) {
			_deleteLater = false;
			deleteLater();
		}
	}
}

bool PopupMenu::popupSubmenuFromAction(QAction *action, int actionTop, TriggeredSource source) {
	if (auto submenu = _submenus.value(action)) {
		if (_activeSubmenu == submenu) {
			submenu->hideMenu(true);
		} else {
			popupSubmenu(submenu, actionTop, source);
		}
		return true;
	}
	return false;
}

void PopupMenu::popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source) {
	if (auto currentSubmenu = base::take(_activeSubmenu)) {
		currentSubmenu->hideMenu(true);
	}
	if (submenu) {
		QPoint p(_inner.x() + (rtl() ? _padding.right() : _inner.width() - _padding.left()), _inner.y() + actionTop);
		_activeSubmenu = submenu;
		_activeSubmenu->showMenu(geometry().topLeft() + p, this, source);

		_menu->setChildShown(true);
	} else {
		_menu->setChildShown(false);
	}
}

void PopupMenu::forwardKeyPress(int key) {
	if (!handleKeyPress(key)) {
		_menu->handleKeyPress(key);
	}
}

bool PopupMenu::handleKeyPress(int key) {
	if (_activeSubmenu) {
		_activeSubmenu->handleKeyPress(key);
		return true;
	} else if (key == Qt::Key_Escape) {
		hideMenu(_parent ? true : false);
		return true;
	} else if (key == (rtl() ? Qt::Key_Right : Qt::Key_Left)) {
		if (_parent) {
			hideMenu(true);
			return true;
		}
	}
	return false;
}

void PopupMenu::handleMouseMove(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseMove(globalPosition);
	}
}

void PopupMenu::handleMousePress(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMousePress(globalPosition);
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
	if (_activeSubmenu) {
		_activeSubmenu->hideMenu(fast);
	}
}

void PopupMenu::childHiding(PopupMenu *child) {
	if (_activeSubmenu && _activeSubmenu == child) {
		_activeSubmenu = SubmenuPointer();
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
	showMenu(p, nullptr, TriggeredSource::Mouse);
}

void PopupMenu::showMenu(const QPoint &p, PopupMenu *parent, TriggeredSource source) {
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

	_menu->setShowSource(source);

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
	for (auto submenu : base::take(_submenus)) {
		delete submenu;
	}
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	if (auto w = App::wnd()) {
		w->onReActivate();
		QTimer::singleShot(200, w, SLOT(onReActivate()));
	}
#endif
}

} // namespace Ui
