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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "title.h"

#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "boxes/contactsbox.h"
#include "boxes/aboutbox.h"

TitleHider::TitleHider(QWidget *parent) : QWidget(parent), _level(0) {
}

void TitleHider::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(_level * st::layerAlpha);
	p.fillRect(App::main()->dlgsWidth(), 0, width() - App::main()->dlgsWidth(), height(), st::layerBg->b);
}

void TitleHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		emit static_cast<TitleWidget*>(parentWidget())->hiderClicked();
	}
}

void TitleHider::setLevel(float64 level) {
	_level = level;
	update();
}

TitleWidget::TitleWidget(MainWindow *window) : TWidget(window)
, wnd(window)
, hideLevel(0)
, hider(0)
, _back(this, st::titleBackButton, lang(lng_menu_back))
, _cancel(this, lang(lng_cancel), st::titleTextButton)
, _settings(this, lang(lng_menu_settings), st::titleTextButton)
, _contacts(this, lang(lng_menu_contacts), st::titleTextButton)
, _about(this, lang(lng_menu_about), st::titleTextButton)
, _lock(this, window)
, _update(this, window, lang(lng_menu_update))
, _minimize(this, window)
, _maximize(this, window)
, _restore(this, window)
, _close(this, window)
, _a_update(animation(this, &TitleWidget::step_update))
, lastMaximized(!(window->windowState() & Qt::WindowMaximized))
{
	setGeometry(0, 0, wnd->width(), st::titleHeight);
	setAttribute(Qt::WA_OpaquePaintEvent);
	_lock.hide();
	_update.hide();
    _cancel.hide();
    _back.hide();
	if (
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		Sandbox::updatingState() == Application::UpdatingReady ||
#endif
		cHasPasscode()
	) {
		showUpdateBtn();
	}
	stateChanged();

	connect(&_back, SIGNAL(clicked()), window, SLOT(hideSettings()));
	connect(&_cancel, SIGNAL(clicked()), this, SIGNAL(hiderClicked()));
	connect(&_settings, SIGNAL(clicked()), window, SLOT(showSettings()));
	connect(&_contacts, SIGNAL(clicked()), this, SLOT(onContacts()));
	connect(&_about, SIGNAL(clicked()), this, SLOT(onAbout()));
	connect(wnd->windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(stateChanged(Qt::WindowState)));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(showUpdateBtn()));
#endif

    if (cPlatform() != dbipWindows) {
        _minimize.hide();
        _maximize.hide();
        _restore.hide();
        _close.hide();
    }
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(QRect(0, 0, width(), st::titleHeight), st::titleBG->b);
	if (!_cancel.isHidden()) {
		p.setPen(st::titleTextButton.color->p);
		p.setFont(st::titleTextButton.font->f);
		bool inlineSwitchChoose = (App::main() && App::main()->selectingPeerForInlineSwitch());
		auto chooseText = lang(inlineSwitchChoose ? lng_inline_switch_choose : lng_forward_choose);
		p.drawText(st::titleMenuOffset - st::titleTextButton.width / 2, st::titleTextButton.textTop + st::titleTextButton.font->ascent, chooseText);
	}
	p.drawSprite(st::titleIconPos, st::titleIconImg);
	if (Adaptive::OneColumn() && !_counter.isNull() && App::main()) {
		p.drawPixmap(st::titleIconPos.x() + st::titleIconImg.pxWidth() - (_counter.width() / cIntRetinaFactor()), st::titleIconPos.y() + st::titleIconImg.pxHeight() - (_counter.height() / cIntRetinaFactor()), _counter);
	}
}

void TitleWidget::step_update(float64 ms, bool timer) {
	float64 phase = sin(M_PI_2 * (ms / st::updateBlinkDuration));
	if (phase < 0) phase = -phase;
	_update.setOverLevel(phase);
}

void TitleWidget::setHideLevel(float64 level) {
	if (level != hideLevel) {
		hideLevel = level;
		if (hideLevel) {
			if (!hider) {
				hider = new TitleHider(this);
				hider->move(0, 0);
				hider->resize(size());
				if (Adaptive::OneColumn()) {
					hider->hide();
				} else {
					hider->show();
				}
			}
			hider->setLevel(hideLevel);
		} else {
			if (hider) hider->deleteLater();
			hider = 0;
		}
	}
}

void TitleWidget::onContacts() {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();

	if (!App::self()) return;
	Ui::showLayer(new ContactsBox());
}

void TitleWidget::onAbout() {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	Ui::showLayer(new AboutBox());
}

TitleWidget::~TitleWidget() {
	delete hider;
	hider = 0;
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	QPoint p(width() - ((cPlatform() == dbipWindows && lastMaximized) ? 0 : st::sysBtnDelta), 0);

	if (!_update.isHidden()) {
		p.setX(p.x() - _update.width());
		_update.move(p);
		if (!_lock.isHidden()) {
			p.setX(p.x() - _lock.width());
			_lock.move(p);
			p.setX(p.x() + _lock.width());
		}
		p.setX(p.x() + _update.width());
	}
	_cancel.move(p.x() - _cancel.width(), 0);

    if (cPlatform() == dbipWindows) {
        p.setX(p.x() - _close.width());
        _close.move(p);

        p.setX(p.x() - _maximize.width());
        _restore.move(p); _maximize.move(p);

        p.setX(p.x() - _minimize.width());
        _minimize.move(p);
    }
	if (_update.isHidden() && !_lock.isHidden()) {
		p.setX(p.x() - _lock.width());
		_lock.move(p);
	}

	_settings.move(st::titleMenuOffset, 0);
	_back.move(st::titleMenuOffset, 0);
	_back.resize((_minimize.isHidden() ? (_update.isHidden() ? width() : _update.x()) : _minimize.x()) - st::titleMenuOffset, _back.height());
	if (MTP::authedId() && _back.isHidden() && _cancel.isHidden() && !App::passcoded()) {
		if (_contacts.isHidden()) _contacts.show();
		_contacts.move(_settings.x() + _settings.width(), 0);
		_about.move(_contacts.x() + _contacts.width(), 0);
	} else {
		if (!_contacts.isHidden()) _contacts.hide();
		if (!MTP::authedId()) _about.move(_settings.x() + _settings.width(), 0);
	}

	if (hider) hider->resize(size());
}

void TitleWidget::updateBackButton() {
	if (App::passcoded()) {
		if (!_cancel.isHidden()) _cancel.hide();
		if (!_back.isHidden()) _back.hide();
		if (!_settings.isHidden()) _settings.hide();
		if (!_contacts.isHidden()) _contacts.hide();
		if (!_about.isHidden()) _about.hide();
		_lock.setSysBtnStyle(st::sysUnlock);
	} else {
		_lock.setSysBtnStyle(st::sysLock);
		if (Adaptive::OneColumn() && App::main() && App::main()->selectingPeer()) {
			_cancel.show();
			if (!_back.isHidden()) _back.hide();
			if (!_settings.isHidden()) _settings.hide();
			if (!_contacts.isHidden()) _contacts.hide();
			if (!_about.isHidden()) _about.hide();
		} else {
			if (!_cancel.isHidden()) _cancel.hide();
			bool authed = (MTP::authedId() > 0);
			if (Adaptive::OneColumn()) {
				if (App::wnd()->needBackButton()) {
					if (_back.isHidden()) _back.show();
					if (!_settings.isHidden()) _settings.hide();
					if (!_contacts.isHidden()) _contacts.hide();
					if (!_about.isHidden()) _about.hide();
				} else {
					if (!_back.isHidden()) _back.hide();
					if (_settings.isHidden()) _settings.show();
					if (authed && _contacts.isHidden()) _contacts.show();
					if (_about.isHidden()) _about.show();
				}
			} else {
				if (!_back.isHidden()) _back.hide();
				if (_settings.isHidden()) _settings.show();
				if (authed && _contacts.isHidden()) _contacts.show();
				if (_about.isHidden()) _about.show();
			}
		}
	}
	showUpdateBtn();
	update();
}

void TitleWidget::updateAdaptiveLayout() {
	updateBackButton();
	if (Adaptive::OneColumn()) {
		updateCounter();
	}
	if (hider) {
		if (Adaptive::OneColumn()) {
			hider->hide();
		} else {
			hider->show();
		}
	}
}

void TitleWidget::updateCounter() {
	if (!Adaptive::OneColumn() || !MTP::authedId()) return;

	int32 counter = App::histories().unreadBadge();
	bool muted = App::histories().unreadOnlyMuted();

	style::color bg = muted ? st::counterMuteBG : st::counterBG;

	if (counter > 0) {
		int32 size = cRetina() ? -32 : -16;
		switch (cScale()) {
		case dbisOneAndQuarter: size = -20; break;
		case dbisOneAndHalf: size = -24; break;
		case dbisTwo: size = -32; break;
		}
		_counter = QPixmap::fromImage(App::wnd()->iconWithCounter(size, counter, bg, false), Qt::ColorOnly);
		_counter.setDevicePixelRatio(cRetinaFactor());
		update(QRect(st::titleIconPos, st::titleIconImg.pxSize()));
	} else {
		if (!_counter.isNull()) {
			_counter = QPixmap();
			update(QRect(st::titleIconPos, st::titleIconImg.pxSize()));
		}
	}
}

void TitleWidget::mousePressEvent(QMouseEvent *e) {
	if (wnd->psHandleTitle()) return;
	if (e->buttons() & Qt::LeftButton) {
		wnd->wStartDrag(e);
		e->accept();
	}
}

void TitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	if (wnd->psHandleTitle()) return;
	Qt::WindowStates s(wnd->windowState());
	if (s.testFlag(Qt::WindowMaximized)) {
		wnd->setWindowState(s & ~Qt::WindowMaximized);
	} else {
		wnd->setWindowState(s | Qt::WindowMaximized);
	}
}

void TitleWidget::stateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) return;
	maximizedChanged(state == Qt::WindowMaximized);
}

void TitleWidget::showUpdateBtn() {
	if (Adaptive::OneColumn() && App::main() && App::main()->selectingPeer()) {
		_cancel.show();
		_lock.hide();
		_update.hide();
		_minimize.hide();
		_restore.hide();
		_maximize.hide();
		_close.hide();
		return;
	}
	if (cHasPasscode()) {
		_lock.show();
	}
	else {
		_lock.hide();
	}
}

void TitleWidget::maximizedChanged(bool maximized, bool force) {
	if (lastMaximized == maximized && !force) return;

	lastMaximized = maximized;

    if (cPlatform() != dbipWindows || !_update.isHidden()) return;
	if (maximized) {
		_maximize.clearState();
	} else {
		_restore.clearState();
	}

	_maximize.setVisible(!maximized);
	_restore.setVisible(maximized);

	resizeEvent(0);
}

HitTestType TitleWidget::hitTest(const QPoint &p) {
	if (App::wnd() && Ui::isLayerShown()) return HitTestNone;

	int x(p.x()), y(p.y()), w(width()), h(height());
	if (!Adaptive::OneColumn() && hider && x >= App::main()->dlgsWidth()) return HitTestNone;

	if (x >= st::titleIconPos.x() && y >= st::titleIconPos.y() && x < st::titleIconPos.x() + st::titleIconImg.pxWidth() && y < st::titleIconPos.y() + st::titleIconImg.pxHeight()) {
		return HitTestIcon;
	} else if (false
		|| (_lock.hitTest(p - _lock.geometry().topLeft()) == HitTestSysButton && _lock.isVisible())
        || (_update.hitTest(p - _update.geometry().topLeft()) == HitTestSysButton && _update.isVisible())
		|| (_minimize.hitTest(p - _minimize.geometry().topLeft()) == HitTestSysButton)
		|| (_maximize.hitTest(p - _maximize.geometry().topLeft()) == HitTestSysButton)
		|| (_restore.hitTest(p - _restore.geometry().topLeft()) == HitTestSysButton)
		|| (_close.hitTest(p - _close.geometry().topLeft()) == HitTestSysButton)
	) {
		return HitTestSysButton;
	} else if (x >= 0 && x < w && y >= 0 && y < h) {
		if (false
			|| (!_back.isHidden() && _back.geometry().contains(x, y))
			|| (!_cancel.isHidden() && _cancel.geometry().contains(x, y))
			|| (!_settings.isHidden() && _settings.geometry().contains(x, y))
			|| (!_contacts.isHidden() && _contacts.geometry().contains(x, y))
			|| (!_about.isHidden() && _about.geometry().contains(x, y))
		) {
			return HitTestClient;
		}
		return HitTestCaption;
	}
	return HitTestNone;
}
