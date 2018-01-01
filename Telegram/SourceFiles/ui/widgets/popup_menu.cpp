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
#include "ui/widgets/popup_menu.h"

#include "ui/widgets/shadow.h"
#include "platform/platform_specific.h"
#include "application.h"
#include "mainwindow.h"
#include "messenger.h"
#include "lang/lang_keys.h"

namespace Ui {

PopupMenu::PopupMenu(QWidget*, const style::PopupMenu &st) : TWidget(nullptr)
, _st(st)
, _menu(this, _st.menu) {
	init();
}

PopupMenu::PopupMenu(QWidget*, QMenu *menu, const style::PopupMenu &st) : TWidget(nullptr)
, _st(st)
, _menu(this, menu, _st.menu) {
	init();

	for (auto action : actions()) {
		if (auto submenu = action->menu()) {
			auto it = _submenus.insert(action, new PopupMenu(nullptr, submenu, st));
			it.value()->deleteOnHide(false);
		}
	}
}

void PopupMenu::init() {
	subscribe(Messenger::Instance().passcodedChanged(), [this] {
		if (App::passcoded()) {
			hideMenu(true);
		}
	});

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
	_menu->setMouseReleaseDelegate([this](QPoint globalPosition) { handleMouseRelease(globalPosition); });

	handleCompositingUpdate();

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::BypassWindowManagerHint | Qt::Popup | Qt::NoDropShadowWindowHint);
	setMouseTracking(true);

	hide();

	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
}

void PopupMenu::handleCompositingUpdate() {
	_padding = _useTransparency ? _st.shadow.extend : style::margins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth);
	_menu->moveToLeft(_padding.left() + _st.scrollPadding.left(), _padding.top() + _st.scrollPadding.top());
	handleMenuResize();
}

void PopupMenu::handleMenuResize() {
	auto newWidth = _padding.left() + _st.scrollPadding.left() + _menu->width() + _st.scrollPadding.right() + _padding.right();
	auto newHeight = _padding.top() + _st.scrollPadding.top() + _menu->height() + _st.scrollPadding.bottom() + _padding.bottom();
	resize(newWidth, newHeight);
	_inner = rect().marginsRemoved(_padding);
}

QAction *PopupMenu::addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, receiver, member, icon, iconOver);
}

QAction *PopupMenu::addAction(const QString &text, base::lambda<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
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

	if (_useTransparency) {
		Platform::StartTranslucentPaint(p, e);
	}

	auto ms = getms();
	if (_a_show.animating(ms)) {
		if (auto opacity = _a_opacity.current(ms, _hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.current(1.), opacity);
		}
	} else if (_a_opacity.animating(ms)) {
		p.setOpacity(_a_opacity.current(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (_showAnimation) {
		_showAnimation->paintFrame(p, 0, 0, width(), 1., 1.);
		_showAnimation.reset();
		showChildren();
	} else {
		paintBg(p);
	}
}

void PopupMenu::paintBg(Painter &p) {
	if (_useTransparency) {
		Shadow::paint(p, _inner, width(), _st.shadow);
		App::roundRect(p, _inner, _st.menu.itemBg, ImageRoundRadius::Small);
	} else {
		p.fillRect(0, 0, width() - _padding.right(), _padding.top(), _st.shadow.fallback);
		p.fillRect(width() - _padding.right(), 0, _padding.right(), height() - _padding.bottom(), _st.shadow.fallback);
		p.fillRect(_padding.left(), height() - _padding.bottom(), width() - _padding.left(), _padding.bottom(), _st.shadow.fallback);
		p.fillRect(0, _padding.top(), _padding.left(), height() - _padding.top(), _st.shadow.fallback);
		p.fillRect(_inner, _st.menu.itemBg);
	}
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
		_triggering = true;
		hideMenu();
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

void PopupMenu::handleMouseRelease(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseRelease(globalPosition);
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
	if (_parent && !_a_opacity.animating()) {
		_parent->childHiding(this);
	}
	if (fast) {
		hideFast();
	} else {
		hideAnimated();
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

void PopupMenu::setOrigin(PanelAnimation::Origin origin) {
	_origin = origin;
}

void PopupMenu::showAnimated(PanelAnimation::Origin origin) {
	setOrigin(origin);
	showStarted();
}

void PopupMenu::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	startOpacityAnimation(true);
}

void PopupMenu::hideFast() {
	if (isHidden()) return;

	_hiding = false;
	_a_opacity.finish();
	hideFinished();
}

void PopupMenu::hideFinished() {
	_a_show.finish();
	_cache = QPixmap();
	if (!isHidden()) {
		hide();
	}
}

void PopupMenu::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	showChildren();
	_cache = GrabWidget(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
}

void PopupMenu::startOpacityAnimation(bool hiding) {
	_hiding = false;
	if (!_useTransparency) {
		_a_opacity.finish();
		if (hiding) {
			hideFinished();
		} else {
			update();
		}
		return;
	}
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., _st.duration);
}

void PopupMenu::showStarted() {
	if (isHidden()) {
		show();
		startShowAnimation();
		return;
	} else if (!_hiding) {
		return;
	}
	startOpacityAnimation(false);
}

void PopupMenu::startShowAnimation() {
	if (!_useTransparency) {
		_a_show.finish();
		update();
		return;
	}
	if (!_a_show.animating()) {
		auto opacityAnimation = base::take(_a_opacity);
		showChildren();
		auto cache = grabForPanelAnimation();
		_a_opacity = base::take(opacityAnimation);

		_showAnimation = std::make_unique<PanelAnimation>(_st.animation, _origin);
		_showAnimation->setFinalImage(std::move(cache), QRect(_inner.topLeft() * cIntRetinaFactor(), _inner.size() * cIntRetinaFactor()));
		if (_useTransparency) {
			auto corners = App::cornersMask(ImageRoundRadius::Small);
			_showAnimation->setCornerMasks(corners[0], corners[1], corners[2], corners[3]);
		} else {
			_showAnimation->setSkipShadow(true);
		}
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { showAnimationCallback(); }, 0., 1., _st.showDuration);
}

void PopupMenu::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else {
			showChildren();
		}
	}
}

void PopupMenu::showAnimationCallback() {
	update();
}

QImage PopupMenu::grabForPanelAnimation() {
	SendPendingMoveResizeEvents(this);
	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		if (_useTransparency) {
			App::roundRect(p, _inner, _st.menu.itemBg, ImageRoundRadius::Small);
		} else {
			p.fillRect(_inner, _st.menu.itemBg);
		}
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->render(&p, widget->pos(), widget->rect(), QWidget::DrawChildren | QWidget::IgnoreMask);
			}
		}
	}
	return result;
}

void PopupMenu::deleteOnHide(bool del) {
	_deleteOnHide = del;
}

void PopupMenu::popup(const QPoint &p) {
	showMenu(p, nullptr, TriggeredSource::Mouse);
}

void PopupMenu::showMenu(const QPoint &p, PopupMenu *parent, TriggeredSource source) {
	_parent = parent;

	auto origin = PanelAnimation::Origin::TopLeft;
	auto w = p - QPoint(0, _padding.top());
	auto r = Sandbox::screenGeometry(p);
	_useTransparency = Platform::TranslucentWindowsSupported(p);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	handleCompositingUpdate();
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
				w.setX(p.x() - width() + _padding.right());
			}
			origin = PanelAnimation::Origin::TopRight;
		}
	}
	if (w.y() + height() - _padding.bottom() > r.y() + r.height()) {
		if (_parent) {
			w.setY(r.y() + r.height() - height() + _padding.bottom());
		} else {
			w.setY(p.y() - height() + _padding.bottom());
			origin = (origin == PanelAnimation::Origin::TopRight) ? PanelAnimation::Origin::BottomRight : PanelAnimation::Origin::BottomLeft;
		}
	}
	if (w.x() < r.x()) {
		w.setX(r.x());
	}
	if (w.y() < r.y()) {
		w.setY(r.y());
	}
	move(w);

	setOrigin(origin);
	_menu->setShowSource(source);

	startShowAnimation();

	psUpdateOverlayed(this);
	show();
	psShowOverAll(this);
	windowHandle()->requestActivate();
	activateWindow();
}

PopupMenu::~PopupMenu() {
	for (auto submenu : base::take(_submenus)) {
		delete submenu;
	}
	if (auto w = App::wnd()) {
		w->reActivateWindow();
	}
	if (_destroyedCallback) {
		_destroyedCallback();
	}
}

} // namespace Ui
