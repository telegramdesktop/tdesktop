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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"
#include "style.h"

#include "title.h"
#include "mainwidget.h"
#include "window.h"
#include "application.h"
#include "boxes/contactsbox.h"
#include "boxes/aboutbox.h"

TitleHider::TitleHider(QWidget *parent) : QWidget(parent), _level(0) {
}

void TitleHider::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(_level * st::layerAlpha);
	p.fillRect(App::main()->dlgsWidth() - st::dlgShadow, 0, width() + st::dlgShadow - App::main()->dlgsWidth(), height(), st::layerBg->b);
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

TitleWidget::TitleWidget(Window *window)
	: QWidget(window)
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
    , lastMaximized(!(window->windowState() & Qt::WindowMaximized))
{
	setGeometry(0, 0, wnd->width(), st::titleHeight);
	setAttribute(Qt::WA_OpaquePaintEvent);
	_lock.hide();
	_update.hide();
    _cancel.hide();
    _back.hide();
	if (App::app()->updatingState() == Application::UpdatingReady || cHasPasscode()) {
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
	connect(App::app(), SIGNAL(updateReady()), this, SLOT(showUpdateBtn()));
	#endif
	
    if (cPlatform() != dbipWindows) {
        _minimize.hide();
        _maximize.hide();
        _restore.hide();
        _close.hide();
    }
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(QRect(0, 0, width(), st::titleHeight), st::titleBG->b);
	if (!_cancel.isHidden()) {
		p.setPen(st::titleTextButton.color->p);
		p.setFont(st::titleTextButton.font->f);
		p.drawText(st::titleMenuOffset - st::titleTextButton.width / 2, st::titleTextButton.textTop + st::titleTextButton.font->ascent, lang(lng_forward_choose));
	}
	p.drawPixmap(st::titleIconPos, App::sprite(), st::titleIconImg);
	if (!cWideMode() && !_counter.isNull() && App::main()) {
		p.drawPixmap(st::titleIconPos.x() + st::titleIconImg.pxWidth() - (_counter.width() / cIntRetinaFactor()), st::titleIconPos.y() + st::titleIconImg.pxHeight() - (_counter.height() / cIntRetinaFactor()), _counter);
	}
}

bool TitleWidget::animStep(float64 ms) {
	float64 phase = sin(M_PI_2 * (ms / st::updateBlinkDuration));
	if (phase < 0) phase = -phase;
	_update.setOverLevel(phase);
	return true;
}

void TitleWidget::setHideLevel(float64 level) {
	if (level != hideLevel) {
		hideLevel = level;
		if (hideLevel) {
			if (!hider) {
				hider = new TitleHider(this);
				hider->move(0, 0);
				hider->resize(size());
				if (cWideMode()) {
					hider->show();
				} else {
					hider->hide();
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
	App::wnd()->showLayer(new ContactsBox());
}

void TitleWidget::onAbout() {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	App::wnd()->showLayer(new AboutBox());
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
		if (!cWideMode() && App::main() && App::main()->selectingPeer()) {
			_cancel.show();
			if (!_back.isHidden()) _back.hide();
			if (!_settings.isHidden()) _settings.hide();
			if (!_contacts.isHidden()) _contacts.hide();
			if (!_about.isHidden()) _about.hide();
		} else {
			if (!_cancel.isHidden()) _cancel.hide();
			bool authed = (MTP::authedId() > 0);
			if (cWideMode()) {
				if (!_back.isHidden()) _back.hide();
				if (_settings.isHidden()) _settings.show();
				if (authed && _contacts.isHidden()) _contacts.show();
				if (_about.isHidden()) _about.show();
			} else {
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
			}
		}
	}
	showUpdateBtn();
	update();
}

void TitleWidget::updateWideMode() {
	updateBackButton();
	if (!cWideMode()) {
		updateCounter();
	}
	if (hider) {
		if (cWideMode()) {
			hider->show();
		} else {
			hider->hide();
		}
	}
}

void TitleWidget::updateCounter() {
	if (cWideMode() || !MTP::authedId()) return;

	int32 counter = App::histories().unreadFull - (cIncludeMuted() ? 0 : App::histories().unreadMuted);
	bool muted = cIncludeMuted() ? (App::histories().unreadMuted >= counter) : false;

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
	if (!cWideMode() && App::main() && App::main()->selectingPeer()) {
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
	} else {
		_lock.hide();
	}
	bool updateReady = App::app()->updatingState() == Application::UpdatingReady;
	if (updateReady || cEvalScale(cConfigScale()) != cEvalScale(cRealScale())) {
		_update.setText(lang(updateReady ? lng_menu_update : lng_menu_restart));
		_update.show();
		resizeEvent(0);
		_minimize.hide();
		_restore.hide();
		_maximize.hide();
		_close.hide();
		anim::start(this);
	} else {
		_update.hide();
		if (cPlatform() == dbipWindows) {
			_minimize.show();
			maximizedChanged(lastMaximized, true);
			_close.show();
		}
		anim::stop(this);
	}
	resizeEvent(0);
	update();
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
	if (App::wnd() && App::wnd()->layerShown()) return HitTestNone;

	int x(p.x()), y(p.y()), w(width()), h(height());
	if (cWideMode() && hider && x >= App::main()->dlgsWidth()) return HitTestNone;

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
