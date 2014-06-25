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
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"

namespace {
	bool frameless = true;
	bool finished = true;

    class _PsEventFilter : public QAbstractNativeEventFilter {
	public:
		_PsEventFilter() {
		}

		bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
			Window *wnd = Application::wnd();
			if (!wnd) return false;

			return false;
		}
	};
    _PsEventFilter *_psEventFilter = 0;

};

void MacPrivate::activeSpaceChanged() {
    if (App::wnd()) {
        App::wnd()->psActivateNotifies();
    }
}

void MacPrivate::notifyClicked(unsigned long long peer) {
    History *history = App::history(PeerId(peer));

    App::wnd()->showFromTray();
    App::wnd()->hideSettings();
    App::main()->showPeer(history->peer->id, false, true);
    App::wnd()->psClearNotify(history);
}

void MacPrivate::notifyReplied(unsigned long long peer, const char *str) {
    History *history = App::history(PeerId(peer));
    
    App::main()->sendMessage(history, QString::fromUtf8(str));
}

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent),
posInited(false), trayIcon(0), trayIconMenu(0), icon256(qsl(":/gui/art/iconround256.png")) {
    connect(&psIdleTimer, SIGNAL(timeout()), this, SLOT(psIdleTimeout()));
    psIdleTimer.setSingleShot(false);
	connect(&notifyWaitTimer, SIGNAL(timeout()), this, SLOT(psNotifyFire()));
	notifyWaitTimer.setSingleShot(true);
}

void PsMainWindow::psNotIdle() const {
	psIdleTimer.stop();
	if (psIdle) {
		psIdle = false;
		if (App::main()) App::main()->setOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	}
}

void PsMainWindow::psIdleTimeout() {
    int64 idleTime = objc_idleTime();
    if (idleTime >= 0) {
        if (idleTime <= IdleMsecs) {
			psNotIdle();
		}
    } else { // error
		psNotIdle();
	}
}

bool PsMainWindow::psIsOnline(int state) const {
	if (state < 0) state = this->windowState();
	if (state & Qt::WindowMinimized) {
		return false;
	} else if (!isVisible()) {
		return false;
	}
    int64 idleTime = objc_idleTime();
    LOG(("App Info: idle time %1").arg(idleTime));
    if (idleTime >= 0) {
        if (idleTime > IdleMsecs) {
			if (!psIdle) {
				psIdle = true;
				psIdleTimer.start(900);
			}
			return false;
		} else {
			psNotIdle();
		}
    } else { // error
		psNotIdle();
	}
	return true;
}

bool PsMainWindow::psIsActive(int state) const {
	if (state < 0) state = this->windowState();
    return isActiveWindow() && isVisible() && !(state & Qt::WindowMinimized) && !psIdle;
}

void PsMainWindow::psRefreshTaskbarIcon() {
}

void PsMainWindow::psUpdateWorkmode() {
}

void PsMainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadFull;

    setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));

    QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
    _private.setWindowBadge(counter ? cnt : QString());
}

void PsMainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		QList<QScreen*> screens = App::app()->screens();
		for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
			QByteArray name = (*i)->name().toUtf8();
			if (pos.moncrc == hashCrc32(name.constData(), name.size())) {
				QRect screen((*i)->geometry());
				int32 w = screen.width(), h = screen.height();
				if (w >= st::wndMinWidth && h >= st::wndMinHeight) {
					if (pos.w > w) pos.w = w;
					if (pos.h > h) pos.h = h;
					pos.x += screen.x();
					pos.y += screen.y();
					if (pos.x < screen.x() + screen.width() - 10 && pos.y < screen.y() + screen.height() - 10) {
						geom = QRect(pos.x, pos.y, pos.w, pos.h);
					}
				}
				break;
			}
		}

		if (pos.y < 0) pos.y = 0;
		maximized = pos.maximized;
	}
	setGeometry(geom);
}

void PsMainWindow::psInitFrameless() {
    psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));

	if (frameless) {
		//setWindowFlags(Qt::FramelessWindowHint);
	}

    connect(windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(psStateChanged(Qt::WindowState)));
}

void PsMainWindow::psSavePosition(Qt::WindowState state) {
    if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !posInited) return;

	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		QRect r(geometry());
		curPos.x = r.x();
		curPos.y = r.y();
		curPos.w = r.width();
		curPos.h = r.height();
		curPos.maximized = 0;
	}

	int px = curPos.x + curPos.w / 2, py = curPos.y + curPos.h / 2, d = 0;
	QScreen *chosen = 0;
	QList<QScreen*> screens = App::app()->screens();
	for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
		int dx = (*i)->geometry().x() + (*i)->geometry().width() / 2 - px; if (dx < 0) dx = -dx;
		int dy = (*i)->geometry().y() + (*i)->geometry().height() / 2 - py; if (dy < 0) dy = -dy;
		if (!chosen || dx + dy < d) {
			d = dx + dy;
			chosen = *i;
		}
	}
	if (chosen) {
		curPos.x -= chosen->geometry().x();
		curPos.y -= chosen->geometry().y();
		QByteArray name = chosen->name().toUtf8();
		curPos.moncrc = hashCrc32(name.constData(), name.size());
	}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			App::writeConfig();
		}
    }
}

void PsMainWindow::psUpdatedPosition() {
    psUpdatedPositionTimer.start(4000);
}

void PsMainWindow::psStateChanged(Qt::WindowState state) {
	psUpdateSysMenu(state);
	psUpdateMargins();
//    if (state == Qt::WindowMinimized && GetWindowLong(ps_hWnd, GWL_HWNDPARENT)) {
//		minimizeToTray();
//    }
    psSavePosition(state);
}

void PsMainWindow::psFirstShow() {
	finished = false;

    psUpdateMargins();

	bool showShadows = true;

	show();
    _private.enableShadow(winId());
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if (cFromAutoStart()) {
		if (cStartMinimized()) {
			setWindowState(Qt::WindowMinimized);
			if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
				hide();
			} else {
				show();
			}
			showShadows = false;
		} else {
			show();
		}
	} else {
		show();
	}
	posInited = true;
}

bool PsMainWindow::psHandleTitle() {
    return false;
}

void PsMainWindow::psInitSysMenu() {
}

void PsMainWindow::psUpdateSysMenu(Qt::WindowState state) {
}

void PsMainWindow::psUpdateMargins() {
}

void PsMainWindow::psFlash() {
    _private.startBounce();
}

PsMainWindow::~PsMainWindow() {
	finished = true;
    psClearNotifyFast();
}

void PsMainWindow::psNotify(History *history, MsgId msgId) {
	if (App::quiting() || !history->notifyFrom) return;
    
	bool haveSetting = (history->peer->notify != UnknownNotifySettings);
	if (haveSetting) {
		if (history->peer->notify != EmptyNotifySettings && history->peer->notify->mute > unixtime()) {
			history->clearNotifyFrom();
			return;
		}
	} else {
		App::wnd()->getNotifySetting(MTP_inputNotifyPeer(history->peer->input));
	}
    
	uint64 ms = getms() + NotifyWaitTimeout;
	notifyWhenAlerts[history].insert(ms);
	if (cDesktopNotify()) {
		NotifyWhenMaps::iterator i = notifyWhenMaps.find(history);
		if (i == notifyWhenMaps.end()) {
			i = notifyWhenMaps.insert(history, NotifyWhenMap());
		}
		if (i.value().constFind(msgId) == i.value().cend()) {
			i.value().insert(msgId, ms);
		}
		NotifyWaiters *addTo = haveSetting ? &notifyWaiters : &notifySettingWaiters;
		if (addTo->constFind(history) == addTo->cend()) {
			addTo->insert(history, NotifyWaiter(msgId, ms));
		}
	}
	if (haveSetting) {
		if (!notifyWaitTimer.isActive()) {
			notifyWaitTimer.start(NotifyWaitTimeout);
		}
	}
}

void PsMainWindow::psNotifyFire() {
	psShowNextNotify();
}

void PsMainWindow::psNotifySettingGot() {
	int32 t = unixtime();
	for (NotifyWaiters::iterator i = notifySettingWaiters.begin(); i != notifySettingWaiters.end();) {
		History *history = i.key();
		if (history->peer->notify == UnknownNotifySettings) {
			++i;
		} else {
			if (history->peer->notify == EmptyNotifySettings || history->peer->notify->mute <= t) {
				notifyWaiters.insert(i.key(), i.value());
			}
			i = notifySettingWaiters.erase(i);
		}
	}
	notifyWaitTimer.stop();
	psShowNextNotify();
}

void PsMainWindow::psClearNotify(History *history) {
	if (!history) {
		for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
			(*i)->unlinkHistory();
		}
        _private.clearNotifies();
		for (NotifyWhenMaps::const_iterator i = notifyWhenMaps.cbegin(), e = notifyWhenMaps.cend(); i != e; ++i) {
			i.key()->clearNotifyFrom();
		}
		notifyWaiters.clear();
		notifySettingWaiters.clear();
		notifyWhenMaps.clear();
		return;
	}
	notifyWaiters.remove(history);
	notifySettingWaiters.remove(history);
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->unlinkHistory(history);
	}
    _private.clearNotifies(history->peer->id);
	notifyWhenMaps.remove(history);
	notifyWhenAlerts.remove(history);
}

void PsMainWindow::psClearNotifyFast() {
	notifyWaiters.clear();
	notifySettingWaiters.clear();
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->deleteLater();
	}
    _private.clearNotifies();
	notifyWindows.clear();
	notifyWhenMaps.clear();
	notifyWhenAlerts.clear();
}

void PsMainWindow::psActivateNotifies() {
    if (cCustomNotifies()) {
        for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            _private.activateWnd((*i)->winId());
        }
    }
}

namespace {
	QRect _monitorRect;
	uint64 _monitorLastGot = 0;
	QRect _desktopRect() {
		uint64 tnow = getms();
		if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
			_monitorLastGot = tnow;
            _monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
		}
		return _monitorRect;
	}
}

void PsMainWindow::psShowNextNotify(PsNotifyWindow *remove) {
	if (App::quiting()) return;
    
	int32 count = NotifyWindows;
	if (remove) {
		for (PsNotifyWindows::iterator i = notifyWindows.begin(), e = notifyWindows.end(); i != e; ++i) {
			if ((*i) == remove) {
				notifyWindows.erase(i);
				break;
			}
		}
	}
    
	uint64 ms = getms(), nextAlert = 0;
	bool alert = false;
	for (NotifyWhenAlerts::iterator i = notifyWhenAlerts.begin(); i != notifyWhenAlerts.end();) {
		while (!i.value().isEmpty() && *i.value().begin() <= ms) {
			i.value().erase(i.value().begin());
			NotifySettingsPtr n = i.key()->peer->notify;
			if (n == EmptyNotifySettings || (n != UnknownNotifySettings && n->mute <= unixtime())) {
				alert = true;
			}
		}
		if (i.value().isEmpty()) {
			i = notifyWhenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > *i.value().begin()) {
				nextAlert = *i.value().begin();
			}
			++i;
		}
	}
	if (alert) {
		psFlash();
		App::playSound();
	}
    
    if (cCustomNotifies()) {
        for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            int32 ind = (*i)->index();
            if (ind < 0) continue;
            --count;
        }
    }
	if (count <= 0 || !cDesktopNotify()) {
		if (nextAlert) {
			notifyWaitTimer.start(nextAlert - ms);
		}
		return;
	}
    
	QRect r = _desktopRect();
	int32 x = r.x() + r.width() - st::notifyWidth - st::notifyDeltaX, y = r.y() + r.height() - st::notifyHeight - st::notifyDeltaY;
	while (count > 0) {
		uint64 next = 0;
		HistoryItem *notifyItem = 0;
		NotifyWaiters::iterator notifyWaiter;
		for (NotifyWaiters::iterator i = notifyWaiters.begin(); i != notifyWaiters.end(); ++i) {
			History *history = i.key();
			if (history->notifyFrom && history->notifyFrom->id != i.value().msg) {
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.end()) {
					history->clearNotifyFrom();
					i = notifyWaiters.erase(i);
					continue;
				}
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->notifyFrom->id);
					if (k != j.value().cend()) {
						i.value().msg = k.key();
						i.value().when = k.value();
						break;
					}
					history->getNextNotifyFrom();
				} while (history->notifyFrom);
			}
			if (!history->notifyFrom) {
				notifyWhenMaps.remove(history);
				i = notifyWaiters.erase(i);
				continue;
			}
			uint64 when = i.value().when;
			if (!notifyItem || next > when) {
				next = when;
				notifyItem = history->notifyFrom;
				notifyWaiter = i;
			}
		}
		if (notifyItem) {
			if (next > ms) {
				if (nextAlert && nextAlert < next) {
					next = nextAlert;
					nextAlert = 0;
				}
				notifyWaitTimer.start(next - ms);
				break;
			} else {
                if (cCustomNotifies()) {
                    PsNotifyWindow *notify = new PsNotifyWindow(notifyItem, x, y);
                    notifyWindows.push_back(notify);
                    notify->hide();
                    _private.holdOnTop(notify->winId());
                    notify->show();
                    _private.showOverAll(notify->winId());
                    --count;
                } else {
                    _private.showNotify(notifyItem->history()->peer->id, notifyItem->history()->peer->name, notifyItem->notificationHeader(), notifyItem->notificationText());
                }
                
				uint64 ms = getms();
				History *history = notifyItem->history();
				history->getNextNotifyFrom();
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.end() || !history->notifyFrom) {
					history->clearNotifyFrom();
					notifyWaiters.erase(notifyWaiter);
					if (j != notifyWhenMaps.end()) notifyWhenMaps.erase(j);
					continue;
				}
				j.value().remove(notifyItem->id);
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->notifyFrom->id);
					if (k != j.value().cend()) {
						notifyWaiter.value().msg = k.key();
						notifyWaiter.value().when = k.value();
						break;
					}
					history->getNextNotifyFrom();
				} while (history->notifyFrom);
				if (!history->notifyFrom) {
					notifyWaiters.erase(notifyWaiter);
					notifyWhenMaps.erase(j);
					continue;
				}
			}
		} else {
			break;
		}
	}
	if (nextAlert) {
		notifyWaitTimer.start(nextAlert - ms);
	}
    
	count = NotifyWindows - count;
	for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		int32 ind = (*i)->index();
		if (ind < 0) continue;
		--count;
		(*i)->moveTo(x, y - count * (st::notifyHeight + st::notifyDeltaY));
	}
}

void PsMainWindow::psStopHiding() {
    if (cCustomNotifies()) {
        for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->stopHiding();
        }
    }
}

void PsMainWindow::psStartHiding() {
    if (cCustomNotifies()) {
        for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->startHiding();
        }
    }
}

void PsMainWindow::psUpdateNotifies() {
    if (cCustomNotifies()) {
        for (PsNotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->updatePeerPhoto();
        }
    }
}

PsNotifyWindow::PsNotifyWindow(HistoryItem *item, int32 x, int32 y) : history(item->history()),// started(GetTickCount()),
close(this, st::notifyClose), alphaDuration(st::notifyFastAnim), posDuration(st::notifyFastAnim), hiding(false), _index(0), aOpacity(0), aOpacityFunc(st::notifyFastAnimFunc), aY(y + st::notifyHeight + st::notifyDeltaY) {
    
	int32 w = st::notifyWidth, h = st::notifyHeight;
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
    if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notifyBG->c);
    
	{
		QPainter p(&img);
		p.setPen(st::notifyBorder->p);
		p.setBrush(Qt::NoBrush);
		p.drawRect(0, 0, w - 1, h - 1);
        
		if (history->peer->photo->loaded()) {
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), history->peer->photo->pix(st::notifyPhotoSize));
		} else {
			MTP::clearLoaderPriorities();
			peerPhoto = history->peer->photo;
			peerPhoto->load(true, true);
		}
        
		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;
        
		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (history->peer->chat) {
			p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgLeft, rectForName.top() + st::dlgChatImgTop), App::sprite(), st::dlgChatImg);
			rectForName.setLeft(rectForName.left() + st::dlgChatImgSkip);
		}
        
		QDateTime now(QDateTime::currentDateTime()), lastTime(item->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt = lastTime.toString(qsl("hh:mm"));
		int32 dtWidth = st::dlgHistFont->m.width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
		p.setFont(st::dlgDateFont->f);
		p.setPen(st::dlgDateColor->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::dlgHistFont->ascent, dt);
        
		const HistoryItem *textCachedFor = 0;
		Text itemTextCache(itemWidth);
		bool active = false;
		item->drawInDialog(p, QRect(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dlgFont->height), active, textCachedFor, itemTextCache);
        
		p.setPen(st::dlgNameColor->p);
		history->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	}
	pm = QPixmap::fromImage(img);
    
	hideTimer.setSingleShot(true);
	connect(&hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));
    
	inputTimer.setSingleShot(true);
	connect(&inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));
    
	connect(&close, SIGNAL(clicked()), this, SLOT(unlinkHistory()));
	close.setAcceptBoth(true);
	close.move(w - st::notifyClose.width - st::notifyClosePos.x(), st::notifyClosePos.y());
	close.show();
    
	aY.start(y);
	setGeometry(x, aY.current(), st::notifyWidth, st::notifyHeight);
    
	aOpacity.start(1);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    
	show();
    
	setWindowOpacity(aOpacity.current());
    
	alphaDuration = posDuration = st::notifyFastAnim;
	anim::start(this);
    
	checkLastInput();
}

void PsNotifyWindow::checkLastInput() {
    // TODO
	if (true) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
}

void PsNotifyWindow::moveTo(int32 x, int32 y, int32 index) {
	if (index >= 0) {
		_index = index;
	}
	move(x, aY.current());
	aY.start(y);
	aOpacity.restart();
	posDuration = st::notifyFastAnim;
	anim::start(this);
}

void PsNotifyWindow::updatePeerPhoto() {
	if (!peerPhoto->isNull() && peerPhoto->loaded()) {
		QImage img(pm.toImage());
		{
			QPainter p(&img);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), peerPhoto->pix(st::notifyPhotoSize));
		}
		peerPhoto = ImagePtr();
		pm = QPixmap::fromImage(img);
		update();
	}
}

void PsNotifyWindow::unlinkHistory(History *hist) {
	if (!hist || hist == history) {
		animHide(st::notifyFastAnim, st::notifyFastAnimFunc);
		history = 0;
		App::wnd()->psShowNextNotify();
	}
}

void PsNotifyWindow::enterEvent(QEvent */*e*/) {
	if (!history) return;
	if (App::wnd()) App::wnd()->psStopHiding();
}

void PsNotifyWindow::leaveEvent(QEvent */*e*/) {
	if (!history) return;
	App::wnd()->psStartHiding();
}

void PsNotifyWindow::startHiding() {
	hideTimer.start(st::notifyWaitShortHide);
}

void PsNotifyWindow::mousePressEvent(QMouseEvent *e) {
	if (!history) return;
	if (e->button() == Qt::RightButton) {
		unlinkHistory();
	} else if (history) {
		App::wnd()->showFromTray();
		App::wnd()->hideSettings();
		App::main()->showPeer(history->peer->id, false, true);
        unlinkHistory();
		e->ignore();
	}
}

void PsNotifyWindow::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.drawPixmap(0, 0, pm);
}

void PsNotifyWindow::animHide(float64 duration, anim::transition func) {
	if (!history) return;
	alphaDuration = duration;
	aOpacityFunc = func;
	aOpacity.start(0);
	aY.restart();
	hiding = true;
	anim::start(this);
}

void PsNotifyWindow::stopHiding() {
	if (!history) return;
	alphaDuration = st::notifyFastAnim;
	aOpacityFunc = st::notifyFastAnimFunc;
	aOpacity.start(1);
	aY.restart();
	hiding = false;
	hideTimer.stop();
	anim::start(this);
}

void PsNotifyWindow::hideByTimer() {
	if (!history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

bool PsNotifyWindow::animStep(float64 ms) {
	float64 dtAlpha = ms / alphaDuration, dtPos = ms / posDuration;
	if (dtAlpha >= 1) {
		aOpacity.finish();
		if (hiding) {
			deleteLater();
		}
	} else {
		aOpacity.update(dtAlpha, aOpacityFunc);
	}
	setWindowOpacity(aOpacity.current());
	if (dtPos >= 1) {
		aY.finish();
	} else {
		aY.update(dtPos, anim::linear);
	}
	move(x(), aY.current());
	update();
	return (dtAlpha < 1 || (!hiding && dtPos < 1));
}

PsNotifyWindow::~PsNotifyWindow() {
	if (App::wnd()) App::wnd()->psShowNextNotify(this);
}

PsApplication::PsApplication(int &argc, char **argv) : QApplication(argc, argv) {
}

void PsApplication::psInstallEventFilter() {
    delete _psEventFilter;
	_psEventFilter = new _PsEventFilter();
    installNativeEventFilter(_psEventFilter);
}

PsApplication::~PsApplication() {
    delete _psEventFilter;
    _psEventFilter = 0;
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const MTPDhelp_appUpdate &update) : reply(0), already(0), full(0) {
	updateUrl = qs(update.vurl);
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

PsUpdateDownloader::PsUpdateDownloader(QThread *thread, const QString &url) : reply(0), already(0), full(0) {
	updateUrl = url;
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

void PsUpdateDownloader::initOutput() {
	QString fileName;
	QRegularExpressionMatch m = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)")).match(updateUrl);
	if (m.hasMatch()) {
		fileName = m.captured(1).replace(QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")), QString());
	}
	if (fileName.isEmpty()) {
		fileName = qsl("tupdate-%1").arg(rand());
	}
	QString dirStr = cWorkingDir() + qsl("tupdates/");
	fileName = dirStr + fileName;
	QFileInfo file(fileName);

	QDir dir(dirStr);
	if (dir.exists()) {
		QFileInfoList all = dir.entryInfoList(QDir::Files);
		for (QFileInfoList::iterator i = all.begin(), e = all.end(); i != e; ++i) {
			if (i->absoluteFilePath() != file.absoluteFilePath()) {
				QFile::remove(i->absoluteFilePath());
			}
		}
	} else {
		dir.mkdir(dir.absolutePath());
	}
	outputFile.setFileName(fileName);
	if (file.exists()) {
		uint64 fullSize = file.size();
		if (fullSize < INT_MAX) {
			int32 goodSize = (int32)fullSize;
			if (goodSize % UpdateChunk) {
				goodSize = goodSize - (goodSize % UpdateChunk);
				if (goodSize) {
					if (outputFile.open(QIODevice::ReadOnly)) {
						QByteArray goodData = outputFile.readAll().mid(0, goodSize);
						outputFile.close();
						if (outputFile.open(QIODevice::WriteOnly)) {
							outputFile.write(goodData);
							outputFile.close();
							
							QMutexLocker lock(&mutex);
							already = goodSize;
						}
					}
				}
			} else {
				QMutexLocker lock(&mutex);
				already = goodSize;
			}
		}
		if (!already) {
			QFile::remove(fileName);
		}
	}
}

void PsUpdateDownloader::start() {
	sendRequest();
}

void PsUpdateDownloader::sendRequest() {
	QNetworkRequest req(updateUrl);
	QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(already) + "-";// + QByteArray::number(already + cUpdateChunk() - 1); 
	req.setRawHeader("Range", rangeHeaderValue);
	req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
	if (reply) reply->deleteLater();
	reply = manager.get(req);
	connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(partFinished(qint64,qint64)));
	connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(partFailed(QNetworkReply::NetworkError)));
	connect(reply, SIGNAL(metaDataChanged()), this, SLOT(partMetaGot()));
}

void PsUpdateDownloader::partMetaGot() {
	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				{
					QMutexLocker lock(&mutex);
					full = m.captured(1).toInt();
				}
				emit App::app()->updateDownloading(already, full);
			}
		}
	}
}

int32 PsUpdateDownloader::ready() {
	QMutexLocker lock(&mutex);
	return already;
}

int32 PsUpdateDownloader::size() {
	QMutexLocker lock(&mutex);
	return full;
}

void PsUpdateDownloader::partFinished(qint64 got, qint64 total) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: Bad HTTP status received in partFinished(): %1").arg(status));
			return fatalFail();
		}
	}

	if (!already && !full) {
		QMutexLocker lock(&mutex);
		full = total;
	}
	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	if (!outputFile.isOpen()) {
		if (!outputFile.open(QIODevice::Append)) {
			LOG(("Update Error: Could not open output file '%1' for appending").arg(outputFile.fileName()));
			return fatalFail();
		}
	}
	QByteArray r = reply->readAll();
	if (!r.isEmpty()) {
		outputFile.write(r);

		QMutexLocker lock(&mutex);
		already += r.size();
	}
	if (got >= total) {
		reply->deleteLater();
		reply = 0;
		outputFile.close();
		unpackUpdate();
	} else {
		emit App::app()->updateDownloading(already, full);
	}
}

void PsUpdateDownloader::partFailed(QNetworkReply::NetworkError e) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	reply->deleteLater();
	reply = 0;
    if (statusCode.isValid()) {
	    int status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			outputFile.close();
			unpackUpdate();
			return;
		}
	}
	LOG(("Update Error: failed to download part starting from %1, error %2").arg(already).arg(e));
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::deleteDir(const QString &dir) {
	objc_deleteDir(dir);
}

void PsUpdateDownloader::fatalFail() {
	clearAll();
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::clearAll() {
	deleteDir(cWorkingDir() + qsl("tupdates"));
}

#ifdef Q_OS_WIN
typedef DWORD VerInt;
typedef WCHAR VerChar;
#else
typedef int VerInt;
typedef wchar_t VerChar;
#endif

void PsUpdateDownloader::unpackUpdate() {
    QByteArray packed;
	if (!outputFile.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}
#ifdef Q_OS_WIN // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif
	QByteArray compressed = outputFile.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return fatalFail();
	}
	outputFile.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyDirPath = cWorkingDir() + qsl("tupdates/ready");
	deleteDir(tempDirPath);
	deleteDir(readyDirPath);

	QDir tempDir(tempDirPath), readyDir(readyDirPath);
	if (tempDir.exists() || readyDir.exists()) {
		LOG(("Update Error: cant clear tupdates/temp or tupdates/ready dir!"));
		return fatalFail();
	}

	uchar sha1Buffer[20];
	bool goodSha1 = !memcmp(compressed.constData() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, compressedLen + hPropsLen + hOriginalSizeLen, sha1Buffer), hShaLen);
	if (!goodSha1) {
		LOG(("Update Error: bad SHA1 hash of update file!"));
		return fatalFail();
	}

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return fatalFail();
	}
    if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		LOG(("Update Error: bad RSA signature of update file!"));
		return fatalFail();
    }
	RSA_free(pbKey);

	QByteArray uncompressed;

	int32 uncompressedLen;
	memcpy(&uncompressedLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	uncompressed.resize(uncompressedLen);

	size_t resultLen = uncompressed.size();
#ifdef Q_OS_WIN // use Lzma SDK for win
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return fatalFail();
	}
#else
	lzma_stream stream = LZMA_STREAM_INIT;

	lzma_ret ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error initializing the decoder: %1 (error code %2)").arg(msg).arg(ret));
		return fatalFail();
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)uncompressed.data();

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		LOG(("Error in decompression, %1 bytes left in _in of %2 whole.").arg(stream.avail_in).arg(compressedLen));
		return fatalFail();
	} else if (stream.avail_out) {
		LOG(("Error in decompression, %1 bytes free left in _out of %2 whole.").arg(stream.avail_out).arg(resultLen));
		return fatalFail();
	}
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
			case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
			case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
			case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error in decompression: %1 (error code %2)").arg(msg).arg(res));
		return fatalFail();
	}
#endif

	tempDir.mkdir(tempDir.absolutePath());

	quint32 version;
	{
		QBuffer buffer(&uncompressed);
		buffer.open(QIODevice::ReadOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		stream >> version;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read version from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (version <= AppVersion) {
			LOG(("Update Error: downloaded version %1 is not greater, than mine %2").arg(version).arg(AppVersion));
			return fatalFail();
		}

		quint32 filesCount;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read files count from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (!filesCount) {
			LOG(("Update Error: update is empty!"));
			return fatalFail();
		}
		for (uint32 i = 0; i < filesCount; ++i) {
			QString relativeName;
			quint32 fileSize;
			QByteArray fileInnerData;
			bool executable = false;

			stream >> relativeName >> fileSize >> fileInnerData;
#if defined Q_OS_MAC || defined Q_OS_LINUX
			stream >> executable;
#endif
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (fileSize != quint32(fileInnerData.size())) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return fatalFail();
			}

			QFile f(tempDirPath + '/' + relativeName);
			if (!QDir().mkpath(QFileInfo(f).absolutePath())) {
				LOG(("Update Error: cant mkpath for file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (f.write(fileInnerData) != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			f.close();
			if (executable) {
				QFileDevice::Permissions p = f.permissions();
				p |= QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther;
				f.setPermissions(p);
			}
		}

		// create tdata/version file
		tempDir.mkdir(QDir(tempDirPath + qsl("/tdata")).absolutePath());
		std::wstring versionString = ((version % 1000) ? QString("%1.%2.%3").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000)).arg(int(version % 1000)) : QString("%1.%2").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000))).toStdWString();

		VerInt versionNum = VerInt(version), versionLen = VerInt(versionString.size() * sizeof(VerChar));
		VerChar versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));		
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
		}
		fVersion.write((const char*)&versionNum, sizeof(VerInt));
		fVersion.write((const char*)&versionLen, sizeof(VerInt));
		fVersion.write((const char*)&versionStr[0], versionLen);
		fVersion.close();
	}
	
	if (!tempDir.rename(tempDir.absolutePath(), readyDir.absolutePath())) {
		LOG(("Update Error: cant rename temp dir '%1' to ready dir '%2'").arg(tempDir.absolutePath()).arg(readyDir.absolutePath()));
		return fatalFail();
	}
	deleteDir(tempDirPath);
	outputFile.remove();

    emit App::app()->updateReady();
}

PsUpdateDownloader::~PsUpdateDownloader() {
	delete reply;
	reply = 0;
}

void psActivateProcess(uint64 pid) {
	objc_activateProgram();
}

QString psCurrentCountry() {
	QString country = objc_currentCountry();
	return country.isEmpty() ? QString::fromLatin1(DefaultCountry) : country;
}

QString psCurrentLanguage() {
	QString lng = objc_currentLang();
	return lng.isEmpty() ? QString::fromLatin1(DefaultLanguage) : lng;
}

QString psAppDataPath() {
	return objc_appDataPath();
}

QString psCurrentExeDirectory(int argc, char *argv[]) {
    QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
    if (!first.isEmpty()) {
        QFileInfo info(first);
        if (info.exists()) {
            QDir result(info.absolutePath() + qsl("/../../.."));
            return result.absolutePath() + '/';
        }
    }
	return QString();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

bool psCheckReadyUpdate() {
    QString readyPath = cWorkingDir() + qsl("tupdates/ready");
	if (!QDir(readyPath).exists()) {
		return false;
	}

	// check ready version
	QString versionPath = readyPath + qsl("/tdata/version");
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		VerInt versionNum;
		if (fVersion.read((char*)&versionNum, sizeof(VerInt)) != sizeof(VerInt)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			PsUpdateDownloader::clearAll();
			return false;
		}
		fVersion.close();
		if (versionNum <= AppVersion) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			PsUpdateDownloader::clearAll();
			return false;
		}
	}

#ifdef Q_OS_WIN
	QString curUpdater = (cExeDir() + "Updater.exe");
	QFileInfo updater(cWorkingDir() + "tupdates/ready/Updater.exe");
#elif defined Q_OS_MAC
	QString curUpdater = (cExeDir() + "Telegram.app/Contents/Frameworks/Updater");
	QFileInfo updater(cWorkingDir() + "tupdates/ready/Telegram.app/Contents/Frameworks/Updater");
#endif
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			PsUpdateDownloader::clearAll();
			return false;
		}
		if (!QFile(current.absoluteFilePath()).copy(updater.absoluteFilePath())) {
			PsUpdateDownloader::clearAll();
			return false;
		}
	}
#ifdef Q_OS_WIN
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
    }
#elif defined Q_OS_MAC
	QFileInfo to(curUpdater);
	QDir().mkpath(to.absolutePath());
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		PsUpdateDownloader::clearAll();
		return false;
	}
#endif
    return true;
}

void psPostprocessFile(const QString &name) {
}

void psOpenFile(const QString &name, bool openWith) {
    objc_openFile(name, openWith);
}

void psShowInFolder(const QString &name) {
    objc_showInFinder(name, QFileInfo(name).absolutePath());
}

void psFinish() {
    objc_finish();
}

void psExecUpdater() {
	if (!objc_execUpdater()) {
		QString readyPath = cWorkingDir() + qsl("tupdates/ready");
		PsUpdateDownloader::deleteDir(readyPath);
	}
}

void psExecTelegram() {
	objc_execTelegram();
}

void psAutoStart(bool start, bool silent) {
}
