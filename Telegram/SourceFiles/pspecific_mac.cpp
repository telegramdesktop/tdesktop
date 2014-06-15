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
/*
			MSG *msg = (MSG*)message;
			if (msg->message == WM_ENDSESSION) {
				App::quit();
				return false;
			}
			if (msg->hwnd == wnd->psHwnd() || msg->hwnd && !wnd->psHwnd()) {
				return mainWindowEvent(msg->hwnd, msg->message, msg->wParam, msg->lParam, (LRESULT*)result);
			}*/
			return false;
		}
/*
		bool mainWindowEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result) {
			if (tbCreatedMsgId && msg == tbCreatedMsgId) {
				if (CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, IID_ITaskbarList3, (void**)&tbListInterface) != S_OK) {
					tbListInterface = 0;
				}
			}
			switch (msg) {

			case WM_DESTROY: {
				App::quit();
			} return false;

			case WM_ACTIVATE: {
				if (LOWORD(wParam) == WA_CLICKACTIVE) {
					App::wnd()->inactivePress(true);
				}
			} return false;

            case WM_SIZE: {
				if (App::wnd()) {
					if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MINIMIZED) {
						if (wParam != SIZE_RESTORED || App::wnd()->windowState() != Qt::WindowNoState) {
							Qt::WindowState state = Qt::WindowNoState;
							if (wParam == SIZE_MAXIMIZED) {
								state = Qt::WindowMaximized;
							} else if (wParam == SIZE_MINIMIZED) {
								state = Qt::WindowMinimized;
							}
							emit App::wnd()->windowHandle()->windowStateChanged(state);
						} else {
							App::wnd()->psUpdatedPosition();
						}
						int changes = (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) ? _PsShadowHidden : (_PsShadowResized | _PsShadowShown);
						_psShadowWindows.update(changes);
					}
				}
			} return false;

			case WM_MOVE: {
				_psShadowWindows.update(_PsShadowMoved);
				App::wnd()->psUpdatedPosition();
			} return false;
            }
			return false;
		}*/
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
    
    //tbCreatedMsgId = RegisterWindowMessage(L"TaskbarButtonCreated");
    icon16 = icon256.scaledToWidth(16, Qt::SmoothTransformation);
    icon32 = icon256.scaledToWidth(32, Qt::SmoothTransformation);
    connect(&psIdleTimer, SIGNAL(timeout()), this, SLOT(psIdleTimeout()));
    psIdleTimer.setSingleShot(false);
	connect(&notifyWaitTimer, SIGNAL(timeout()), this, SLOT(psNotifyFire()));
	notifyWaitTimer.setSingleShot(true);
}

void PsMainWindow::psIdleTimeout() {
    int64 idleTime = _idleTime();
    if (idleTime >= 0) {
        if (idleTime <= IdleMsecs) {
            psIdle = false;
            psIdleTimer.stop();
            if (App::main()) App::main()->setOnline();
		}
    }
}

bool PsMainWindow::psIsOnline(int state) const {
	if (state < 0) state = this->windowState();
	if (state & Qt::WindowMinimized) {
		return false;
	} else if (!isVisible()) {
		return false;
	}
    int64 idleTime = _idleTime();
    LOG(("App Info: idle time %1").arg(idleTime));
    if (idleTime >= 0) {
        if (idleTime > IdleMsecs) {
			if (!psIdle) {
				psIdle = true;
				psIdleTimer.start(900);
			}
			return false;
		} else {
			psIdle = false;
			psIdleTimer.stop();
		}
    }
	return true;
}

bool PsMainWindow::psIsActive(int state) const {
	if (state < 0) state = this->windowState();
    return isActiveWindow() && isVisible() && !(state & Qt::WindowMinimized);
}

void PsMainWindow::psRefreshTaskbarIcon() {
    /*QWidget *w = new QWidget(this);
	w->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
	w->setGeometry(x() + 1, y() + 1, 1, 1);
	QPalette p(w->palette());
	p.setColor(QPalette::Background, st::titleBG->c);
	QWindow *wnd = w->windowHandle();
	w->setPalette(p);
	w->show();
	w->activateWindow();
    delete w;*/
}

void PsMainWindow::psUpdateWorkmode() {
    /*switch (cWorkMode()) {
	case dbiwmWindowAndTray: {
		setupTrayIcon();
		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;

	case dbiwmTrayOnly: {
		setupTrayIcon();
		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (!psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, (LONG)ps_tbHider_hWnd);
		}
	} break;

	case dbiwmWindowOnly: {
		if (trayIconMenu) trayIconMenu->deleteLater();
		trayIconMenu = 0;
		if (trayIcon) trayIcon->deleteLater();
		trayIcon = 0;

		HWND psOwner = (HWND)GetWindowLong(ps_hWnd, GWL_HWNDPARENT);
		if (psOwner) {
			SetWindowLong(ps_hWnd, GWL_HWNDPARENT, 0);
			psRefreshTaskbarIcon();
		}
	} break;
    }*/
}

void PsMainWindow::psUpdateCounter() {
	int32 counter = App::histories().unreadFull;

    setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));

    QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
    _private.setWindowBadge(counter ? cnt.toUtf8().constData() : "");
}

/*namespace {
	HMONITOR enumMonitor = 0;
	RECT enumMonitorWork;

	BOOL CALLBACK _monitorEnumProc(
	  _In_  HMONITOR hMonitor,
	  _In_  HDC hdcMonitor,
	  _In_  LPRECT lprcMonitor,
	  _In_  LPARAM dwData
	) {
		MONITORINFOEX info;
		info.cbSize = sizeof(info);
		GetMonitorInfo(hMonitor, &info);
		if (dwData == hashCrc32(info.szDevice, sizeof(info.szDevice))) {
			enumMonitor = hMonitor;
			enumMonitorWork = info.rcWork;
			return FALSE;
		}
		return TRUE;
	}
}*/

void PsMainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	if (cDebug()) { // temp while design
		pos.w = 800;
		pos.h = 600;
	}
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		if (pos.y < 0) pos.y = 0;
        //enumMonitor = 0;
        //EnumDisplayMonitors(0, 0, &_monitorEnumProc, pos.moncrc);
        /*if (enumMonitor) {
			int32 w = enumMonitorWork.right - enumMonitorWork.left, h = enumMonitorWork.bottom - enumMonitorWork.top;
			if (w >= st::wndMinWidth && h >= st::wndMinHeight) {
				if (pos.w > w) pos.w = w;
				if (pos.h > h) pos.h = h;
				pos.x += enumMonitorWork.left;
				pos.y += enumMonitorWork.top;
				if (pos.x < enumMonitorWork.right - 10 && pos.y < enumMonitorWork.bottom - 10) {
					geom = QRect(pos.x, pos.y, pos.w, pos.h);
				}
			}
        }*/
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
/*
	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		RECT w;
		GetWindowRect(ps_hWnd, &w);
		curPos.x = w.left;
		curPos.y = w.top;
		curPos.w = w.right - w.left;
		curPos.h = w.bottom - w.top;
		curPos.maximized = 0;
	}

	HMONITOR hMonitor = MonitorFromWindow(ps_hWnd, MONITOR_DEFAULTTONEAREST);
	if (hMonitor) {
		MONITORINFOEX info;
		info.cbSize = sizeof(info);
		GetMonitorInfo(hMonitor, &info);
		if (!curPos.maximized) {
			curPos.x -= info.rcWork.left;
			curPos.y -= info.rcWork.top;
		}
		curPos.moncrc = hashCrc32(info.szDevice, sizeof(info.szDevice));
	}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			App::writeConfig();
		}
    }*/
}

void PsMainWindow::psUpdatedPosition() {
    //psUpdatedPositionTimer.start(4000);
}

void PsMainWindow::psStateChanged(Qt::WindowState state) {
	psUpdateSysMenu(state);
	psUpdateMargins();
    /*if (state == Qt::WindowMinimized && GetWindowLong(ps_hWnd, GWL_HWNDPARENT)) {
		minimizeToTray();
    }
    psSavePosition(state);*/
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
                    _private.showNotify(notifyItem->history()->peer->id, notifyItem->history()->peer->name.toUtf8().constData(), notifyItem->notificationHeader().toUtf8().constData(), notifyItem->notificationText().toUtf8().constData());
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

PsNotifyWindow::PsNotifyWindow(HistoryItem *item, int32 x, int32 y) : history(item->history()), aOpacity(0), _index(0), hiding(false),// started(GetTickCount()),
alphaDuration(st::notifyFastAnim), posDuration(st::notifyFastAnim), aY(y + st::notifyHeight + st::notifyDeltaY), close(this, st::notifyClose), aOpacityFunc(st::notifyFastAnimFunc) {
    
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
    /*std::wstring wDir = QDir::toNativeSeparators(dir).toStdWString();
	WCHAR path[4096];
	memcpy(path, wDir.c_str(), (wDir.size() + 1) * sizeof(WCHAR));
	path[wDir.size() + 1] = 0;
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
    int res = SHFileOperation(&file_op);*/
}

void PsUpdateDownloader::fatalFail() {
	clearAll();
	emit App::app()->updateFailed();
}

void PsUpdateDownloader::clearAll() {
	deleteDir(cWorkingDir() + qsl("tupdates"));
}

void PsUpdateDownloader::unpackUpdate() {
    /*QByteArray packed;
	if (!outputFile.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}

	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header

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
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return fatalFail();
	}

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
		for (int32 i = 0; i < filesCount; ++i) {
			QString relativeName;
			quint32 fileSize;
			QByteArray fileInnerData;

			stream >> relativeName >> fileSize >> fileInnerData;
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (fileSize != fileInnerData.size()) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return fatalFail();
			}

			QFile f(tempDirPath + '/' + relativeName);
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
		}

		// create tdata/version file
		tempDir.mkdir(QDir(tempDirPath + qsl("/tdata")).absolutePath());
		std::wstring versionString = ((version % 1000) ? QString("%1.%2.%3").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000)).arg(int(version % 1000)) : QString("%1.%2").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000))).toStdWString();
		DWORD versionNum = DWORD(version), versionLen = DWORD(versionString.size() * sizeof(WCHAR));
		WCHAR versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));		
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
		}
		fVersion.write((const char*)&versionNum, sizeof(DWORD));
		fVersion.write((const char*)&versionLen, sizeof(DWORD));
		fVersion.write((const char*)&versionStr[0], versionLen);
		fVersion.close();
	}
	
	if (!tempDir.rename(tempDir.absolutePath(), readyDir.absolutePath())) {
		LOG(("Update Error: cant rename temp dir '%1' to ready dir '%2'").arg(tempDir.absolutePath()).arg(readyDir.absolutePath()));
		return fatalFail();
	}
	deleteDir(tempDirPath);
	outputFile.remove();

    emit App::app()->updateReady();*/
}

PsUpdateDownloader::~PsUpdateDownloader() {
	delete reply;
	reply = 0;
}

/*namespace {
	BOOL CALLBACK _ActivateProcess(HWND hWnd, LPARAM lParam) {
		uint64 &processId(*(uint64*)lParam);

		DWORD dwProcessId;
		::GetWindowThreadProcessId(hWnd, &dwProcessId);

		if ((uint64)dwProcessId == processId) { // found top-level window
			static const int32 nameBufSize = 1024;
			WCHAR nameBuf[nameBufSize];
			int32 len = GetWindowText(hWnd, nameBuf, nameBufSize);
			if (len && len < nameBufSize) {
				if (QRegularExpression(qsl("^Telegram(\\s*\\(\\d+\\))?$")).match(QString::fromStdWString(nameBuf)).hasMatch()) {
					BOOL res = ::SetForegroundWindow(hWnd);
					return FALSE;
				}
			}
		}
		return TRUE;
	}
}*/

void psActivateProcess(uint64 pid) {
    //::EnumWindows((WNDENUMPROC)_ActivateProcess, (LPARAM)&pid);
}

QString psCurrentCountry() {
    /*int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrCountry[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wstrCountry, chCount);
		return len ? QString::fromStdWString(std::wstring(wstrCountry)) : QString::fromLatin1(DefaultCountry);
	}
    return QString::fromLatin1(DefaultCountry);*/
    return QString("");
    //TODO
}

/*namespace {
    QString langById(int lngId) {
		int primary = lngId & 0xFF;
		switch (primary) {
			case 0x36: return qsl("af");
			case 0x1C: return qsl("sq");
			case 0x5E: return qsl("am");
			case 0x01: return qsl("ar");
			case 0x2B: return qsl("hy");
			case 0x4D: return qsl("as");
			case 0x2C: return qsl("az");
			case 0x45: return qsl("bn");
			case 0x6D: return qsl("ba");
			case 0x2D: return qsl("eu");
			case 0x23: return qsl("be");
			case 0x1A:
				if (lngId == LANG_CROATIAN) {
					return qsl("hr");
				} else if (lngId == LANG_BOSNIAN_NEUTRAL || lngId == LANG_BOSNIAN) {
					return qsl("bs");
				}
				return qsl("sr");
			break;
			case 0x7E: return qsl("br");
			case 0x02: return qsl("bg");
			case 0x92: return qsl("ku");
			case 0x03: return qsl("ca");
			case 0x04: return qsl("zh");
			case 0x83: return qsl("co");
			case 0x05: return qsl("cs");
			case 0x06: return qsl("da");
			case 0x65: return qsl("dv");
			case 0x13: return qsl("nl");
			case 0x09: return qsl("en");
			case 0x25: return qsl("et");
			case 0x38: return qsl("fo");
			case 0x0B: return qsl("fi");
			case 0x0c: return qsl("fr");
			case 0x62: return qsl("fy");
			case 0x56: return qsl("gl");
			case 0x37: return qsl("ka");
			case 0x07: return qsl("de");
			case 0x08: return qsl("el");
			case 0x6F: return qsl("kl");
			case 0x47: return qsl("gu");
			case 0x68: return qsl("ha");
			case 0x0D: return qsl("he");
			case 0x39: return qsl("hi");
			case 0x0E: return qsl("hu");
			case 0x0F: return qsl("is");
			case 0x70: return qsl("ig");
			case 0x21: return qsl("id");
			case 0x5D: return qsl("iu");
			case 0x3C: return qsl("ga");
			case 0x34: return qsl("xh");
			case 0x35: return qsl("zu");
			case 0x10: return qsl("it");
			case 0x11: return qsl("ja");
			case 0x4B: return qsl("kn");
			case 0x3F: return qsl("kk");
			case 0x53: return qsl("kh");
			case 0x87: return qsl("rw");
			case 0x12: return qsl("ko");
			case 0x40: return qsl("ky");
			case 0x54: return qsl("lo");
			case 0x26: return qsl("lv");
			case 0x27: return qsl("lt");
			case 0x6E: return qsl("lb");
			case 0x2F: return qsl("mk");
			case 0x3E: return qsl("ms");
			case 0x4C: return qsl("ml");
			case 0x3A: return qsl("mt");
			case 0x81: return qsl("mi");
			case 0x4E: return qsl("mr");
			case 0x50: return qsl("mn");
			case 0x61: return qsl("ne");
			case 0x14: return qsl("no");
			case 0x82: return qsl("oc");
			case 0x48: return qsl("or");
			case 0x63: return qsl("ps");
			case 0x29: return qsl("fa");
			case 0x15: return qsl("pl");
			case 0x16: return qsl("pt");
			case 0x67: return qsl("ff");
			case 0x46: return qsl("pa");
			case 0x18: return qsl("ro");
			case 0x17: return qsl("rm");
			case 0x19: return qsl("ru");
			case 0x3B: return qsl("se");
			case 0x4F: return qsl("sa");
			case 0x32: return qsl("tn");
			case 0x59: return qsl("sd");
			case 0x5B: return qsl("si");
			case 0x1B: return qsl("sk");
			case 0x24: return qsl("sl");
			case 0x0A: return qsl("es");
			case 0x41: return qsl("sw");
			case 0x1D: return qsl("sv");
			case 0x28: return qsl("tg");
			case 0x49: return qsl("ta");
			case 0x44: return qsl("tt");
			case 0x4A: return qsl("te");
			case 0x1E: return qsl("th");
			case 0x51: return qsl("bo");
			case 0x73: return qsl("ti");
			case 0x1F: return qsl("tr");
			case 0x42: return qsl("tk");
			case 0x22: return qsl("uk");
			case 0x20: return qsl("ur");
			case 0x80: return qsl("ug");
			case 0x43: return qsl("uz");
			case 0x2A: return qsl("vi");
			case 0x52: return qsl("cy");
			case 0x88: return qsl("wo");
			case 0x78: return qsl("ii");
			case 0x6A: return qsl("yo");
		}
		return QString::fromLatin1(DefaultLanguage);
	}
}*/

QString psCurrentLanguage() {
/*	int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrLocale[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SNAME, wstrLocale, chCount);
		if (!len) return QString::fromLatin1(DefaultLanguage);
		QString locale = QString::fromStdWString(std::wstring(wstrLocale));
		QRegularExpressionMatch m = QRegularExpression("(^|[^a-z])([a-z]{2})-").match(locale);
		if (m.hasMatch()) {
			return m.captured(2);
		}
	}
	chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrLocale[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, wstrLocale, chCount), lngId = 0;
		if (len < 5) return QString::fromLatin1(DefaultLanguage);

		for (int i = 0; i < 4; ++i) {
			WCHAR ch = wstrLocale[i];
			lngId *= 16;
			if (ch >= WCHAR('0') && ch <= WCHAR('9')) {
				lngId += (ch - WCHAR('0'));
			} else if (ch >= WCHAR('A') && ch <= WCHAR('F')) {
				lngId += (10 + ch - WCHAR('A'));
			} else {
				return QString::fromLatin1(DefaultLanguage);
			}
		}
		return langById(lngId);
	}
    return QString::fromLatin1(DefaultLanguage);*/
    return QString("en");
}

QString psAppDataPath() {
    /*static const int maxFileLen = MAX_PATH * 10;
	WCHAR wstrPath[maxFileLen];
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
		QDir appData(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath() + "/" + QString::fromWCharArray(AppName) + "/";
    }*/
	return QString();
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
    /*__try
	{
		psDoCleanup();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
    }*/
	return 0;
}

void psDoFixPrevious() {
    /*try {
		static const int bufSize = 4096;
		DWORD checkType, checkSize = bufSize * 2;
		WCHAR checkStr[bufSize];

		QString appId = QString::fromStdWString(AppId);
		QString newKeyStr1 = QString("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString newKeyStr2 = QString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr1 = QString("SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		QString oldKeyStr2 = QString("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%1_is1").arg(appId);
		HKEY newKey1, newKey2, oldKey1, oldKey2;
		LSTATUS newKeyRes1 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr1.toStdWString().c_str(), 0, KEY_READ, &newKey1);
		LSTATUS newKeyRes2 = RegOpenKeyEx(HKEY_CURRENT_USER, newKeyStr2.toStdWString().c_str(), 0, KEY_READ, &newKey2);
		LSTATUS oldKeyRes1 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str(), 0, KEY_READ, &oldKey1);
		LSTATUS oldKeyRes2 = RegOpenKeyEx(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str(), 0, KEY_READ, &oldKey2);
		
		bool existNew1 = (newKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(newKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existNew2 = (newKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(newKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld1 = (oldKeyRes1 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey1, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;
		bool existOld2 = (oldKeyRes2 == ERROR_SUCCESS) && (RegQueryValueEx(oldKey2, L"InstallDate", 0, &checkType, (BYTE*)checkStr, &checkSize) == ERROR_SUCCESS); checkSize = bufSize * 2;

		if (newKeyRes1 == ERROR_SUCCESS) RegCloseKey(newKey1);
		if (newKeyRes2 == ERROR_SUCCESS) RegCloseKey(newKey2);
		if (oldKeyRes1 == ERROR_SUCCESS) RegCloseKey(oldKey1);
		if (oldKeyRes2 == ERROR_SUCCESS) RegCloseKey(oldKey2);

		if (existNew1 || existNew2) {
			oldKeyRes1 = existOld1 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr1.toStdWString().c_str()) : ERROR_SUCCESS;
			oldKeyRes2 = existOld2 ? RegDeleteKey(HKEY_LOCAL_MACHINE, oldKeyStr2.toStdWString().c_str()) : ERROR_SUCCESS;
		}

		QString userDesktopLnk, commonDesktopLnk;
		WCHAR userDesktopFolder[MAX_PATH], commonDesktopFolder[MAX_PATH];
		HRESULT userDesktopRes = SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, userDesktopFolder);
		HRESULT commonDesktopRes = SHGetFolderPath(0, CSIDL_COMMON_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, commonDesktopFolder);
		if (SUCCEEDED(userDesktopRes)) {
			userDesktopLnk = QString::fromWCharArray(userDesktopFolder) + "\\Telegram.lnk";
		}
		if (SUCCEEDED(commonDesktopRes)) {
			commonDesktopLnk = QString::fromWCharArray(commonDesktopFolder) + "\\Telegram.lnk";
		}
		QFile userDesktopFile(userDesktopLnk), commonDesktopFile(commonDesktopLnk);
		if (QFile::exists(userDesktopLnk) && QFile::exists(commonDesktopLnk) && userDesktopLnk != commonDesktopLnk) {
			bool removed = QFile::remove(commonDesktopLnk);
		}
	} catch (...) {
    }*/
}

int psFixPrevious() {
    /*__try
	{
		psDoFixPrevious();
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
    }*/
	return 0;
}

bool psCheckReadyUpdate() {
    /*QString readyPath = cWorkingDir() + qsl("tupdates/ready");
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
		DWORD versionNum;
		if (fVersion.read((char*)&versionNum, sizeof(DWORD)) != sizeof(DWORD)) {
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

	QString curUpdater = (cExeDir() + "Updater.exe");
	QFileInfo updater(cWorkingDir() + "tupdates/ready/Updater.exe");
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			PsUpdateDownloader::clearAll();
			return false;
		}
		if (CopyFile(current.absoluteFilePath().toStdWString().c_str(), updater.absoluteFilePath().toStdWString().c_str(), TRUE) == FALSE) {
			PsUpdateDownloader::clearAll();
			return false;
		}
	}
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		PsUpdateDownloader::clearAll();
		return false;
    }*/
    return false; // TODO
}

void psPostprocessFile(const QString &name) {
    /*std::wstring zoneFile = QDir::toNativeSeparators(name).toStdWString() + L":Zone.Identifier";
	HANDLE f = CreateFile(zoneFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (f == INVALID_HANDLE_VALUE) { // :(
		return;
	}

	const char data[] = "[ZoneTransfer]\r\nZoneId=3\r\n";

	DWORD written = 0;
	BOOL result = WriteFile(f, data, sizeof(data), &written, NULL);
	CloseHandle(f);

	if (!result || written != sizeof(data)) { // :(
		return;
    }*/
}

void psOpenFile(const QString &name, bool openWith) {
    /*std::wstring wname = QDir::toNativeSeparators(name).toStdWString();

	if (openWith && useOpenAs) {
		if (shOpenWithDialog) {
			OPENASINFO info;
			info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC;
			info.pcszClass = NULL;
			info.pcszFile = wname.c_str();
			shOpenWithDialog(0, &info);
		} else {
			openAs_RunDLL(0, 0, wname.c_str(), SW_SHOWNORMAL);
		}
	} else {
		ShellExecute(0, L"open", wname.c_str(), 0, 0, SW_SHOWNORMAL);
    }*/
}

void psShowInFolder(const QString &name) {
    //QString nameEscaped = QDir::toNativeSeparators(name).replace('"', qsl("\"\""));
    //ShellExecute(0, 0, qsl("explorer").toStdWString().c_str(), (qsl("/select,") + nameEscaped).toStdWString().c_str(), 0, SW_SHOWNORMAL);
}

void psExecUpdater() {
    /*QString targs = qsl("-update");
	if (cFromAutoStart()) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");

	QString updater(QDir::toNativeSeparators(cExeDir() + "Updater.exe")), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Updater.exe").arg(targs));
	HINSTANCE r = ShellExecute(0, 0, updater.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(updater).arg(wdir).arg(long(r)));
		QString readyPath = cWorkingDir() + qsl("tupdates/ready");
		PsUpdateDownloader::deleteDir(readyPath);
    }*/
}

void psExecTelegram() {
    /*QString targs = qsl("-noupdate -tosettings");
	if (cFromAutoStart()) targs += qsl(" -autostart");
	if (cDebug()) targs += qsl(" -debug");
	if (cDataFile() != (cTestMode() ? qsl("data_test") : qsl("data"))) targs += qsl(" -key \"") + cDataFile() + '"';

	QString telegram(QDir::toNativeSeparators(cExeDir() + "Telegram.exe")), wdir(QDir::toNativeSeparators(cWorkingDir()));

	DEBUG_LOG(("Application Info: executing %1 %2").arg(cExeDir() + "Telegram.exe").arg(targs));
	HINSTANCE r = ShellExecute(0, 0, telegram.toStdWString().c_str(), targs.toStdWString().c_str(), wdir.isEmpty() ? 0 : wdir.toStdWString().c_str(), SW_SHOWNORMAL);
	if (long(r) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3").arg(telegram).arg(wdir).arg(long(r)));
    }*/
}

void psAutoStart(bool start, bool silent) {
    /*WCHAR startupFolder[MAX_PATH];
	HRESULT hres = SHGetFolderPath(0, CSIDL_STARTUP, 0, SHGFP_TYPE_CURRENT, startupFolder);
	if (SUCCEEDED(hres)) {
		QString lnk = QString::fromWCharArray(startupFolder) + "\\Telegram.lnk"; 
		if (start) {
			IShellLink* psl; 
			hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
			if (SUCCEEDED(hres)) { 
				IPersistFile* ppf; 
 
				QString exe = QDir::toNativeSeparators(QDir(cExeDir()).absolutePath() + "//Telegram.exe"), dir = QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
				psl->SetArguments(L"-autostart");
				psl->SetPath(exe.toStdWString().c_str());
				psl->SetWorkingDirectory(dir.toStdWString().c_str());
				psl->SetDescription(L"Telegram autorun link.\nYou can disable autorun in Telegram settings."); 
 
				hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 
 
				if (SUCCEEDED(hres)) { 
					hres = ppf->Save(lnk.toStdWString().c_str(), TRUE); 
					ppf->Release(); 
				}  else {
					if (!silent) LOG(("App Error: could not create interface IID_IPersistFile %1").arg(hres));
				}
				psl->Release(); 
			} else {
				if (!silent) LOG(("App Error: could not create instance of IID_IShellLink %1").arg(hres));
			}
		} else {
			QFile::remove(lnk);
		}
	} else {
		if (!silent) LOG(("App Error: could not get CSIDL_STARTUP folder %1").arg(hres));
    }*/
}
