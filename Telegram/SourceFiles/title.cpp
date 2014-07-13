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
	p.fillRect(App::main()->dlgsWidth() - 1, 0, width() - App::main()->dlgsWidth(), height(), st::layerBG->b);
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
	, _settings(this, lang(lng_menu_settings), st::titleTextButton)
	, _contacts(this, lang(lng_menu_contacts), st::titleTextButton)
	, _about(this, lang(lng_menu_about), st::titleTextButton)
	, _update(this, window)
	, _minimize(this, window)
	, _maximize(this, window)
	, _restore(this, window)
	, _close(this, window)
    , lastMaximized(!(window->windowState() & Qt::WindowMaximized))
{

	setGeometry(0, 0, wnd->width(), st::titleHeight);
	stateChanged();

	if (App::app()->updatingState() == Application::UpdatingReady) {
		_update.show();
	} else {
		_update.hide();
	}

	connect(&_settings, SIGNAL(clicked()), window, SLOT(showSettings()));
	connect(&_contacts, SIGNAL(clicked()), this, SLOT(onContacts()));
	connect(&_about, SIGNAL(clicked()), this, SLOT(onAbout()));
	connect(wnd->windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(stateChanged(Qt::WindowState)));
	connect(App::app(), SIGNAL(updateReady()), this, SLOT(showUpdateBtn()));
        
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
	p.drawPixmap(st::titleIconPos, App::sprite(), st::titleIconRect);
}

void TitleWidget::setHideLevel(float64 level) {
	if (level != hideLevel) {
		hideLevel = level;
		if (hideLevel) {
			if (!hider) {
				hider = new TitleHider(this);
				hider->move(0, 0);
				hider->resize(size());
				hider->show();
			}
			hider->setLevel(hideLevel);
		} else {
			if (hider) hider->deleteLater();
			hider = 0;
		}
	}
}

void TitleWidget::onContacts() {
	if (!App::self()) return;
	App::wnd()->showLayer(new ContactsBox());
}

void TitleWidget::onAbout() {
	App::wnd()->showLayer(new AboutBox());
}

TitleWidget::~TitleWidget() {
	delete hider;
	hider = 0;
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	QPoint p(width() - ((cPlatform() == dbipWindows && lastMaximized) ? 0 : st::sysBtnDelta), 0);
	
    if (cPlatform() == dbipWindows) {
        p.setX(p.x() - _close.width());
        _close.move(p);
        
        p.setX(p.x() - _maximize.width());
        _restore.move(p); _maximize.move(p);
        
        p.setX(p.x() - _minimize.width());
        _minimize.move(p);
    }
    
	if (!_update.isHidden()) {
		p.setX(p.x() - _update.width());
		_update.move(p);
	}

	_settings.move(st::titleMenuOffset, 0);
	if (MTP::authedId()) {
		_contacts.show();
		_contacts.move(_settings.x() + _settings.width(), 0);
		_about.move(_contacts.x() + _contacts.width(), 0);
	} else {
		_contacts.hide();
		_about.move(_settings.x() + _settings.width(), 0);
	}

	if (hider) hider->resize(size());
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
	if (App::app()->updatingState() == Application::UpdatingReady || cEvalScale(cConfigScale()) != cEvalScale(cRealScale())) {
		_update.show();
	} else {
		_update.hide();
	}
	resizeEvent(0);
	update();
}

void TitleWidget::maximizedChanged(bool maximized) {
	if (lastMaximized == maximized) return;

	lastMaximized = maximized;

    if (cPlatform() == dbipMac) return;
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

	int x(p.x()), y(p.y()), w(width()), h(height() - st::titleShadow);
	if (hider && x >= App::main()->dlgsWidth()) return HitTestNone;

	if (x >= st::titleIconPos.x() && y >= st::titleIconPos.y() && x < st::titleIconPos.x() + st::titleIconRect.pxWidth() && y < st::titleIconPos.y() + st::titleIconRect.pxHeight()) {
		return HitTestIcon;
	} else if (false
        || (_update.hitTest(p - _update.geometry().topLeft()) == HitTestSysButton && _update.isVisible())
		|| (_minimize.hitTest(p - _minimize.geometry().topLeft()) == HitTestSysButton)
		|| (_maximize.hitTest(p - _maximize.geometry().topLeft()) == HitTestSysButton)
		|| (_restore.hitTest(p - _restore.geometry().topLeft()) == HitTestSysButton)
		|| (_close.hitTest(p - _close.geometry().topLeft()) == HitTestSysButton)
	) {
		return HitTestSysButton;
	} else if (x >= 0 && x < w && y >= 0 && y < h) {
		if (false
			|| _settings.geometry().contains(x, y)
			|| (!_contacts.isHidden() && _contacts.geometry().contains(x, y))
			|| _about.geometry().contains(x, y)
		) {
			return HitTestClient;
		}
		return HitTestCaption;
	}
	return HitTestNone;
}
