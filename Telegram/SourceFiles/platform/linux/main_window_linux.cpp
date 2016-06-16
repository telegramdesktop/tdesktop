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
#include "platform/linux/main_window_linux.h"

#include "mainwindow.h"
#include "application.h"
#include "lang.h"
#include "localstorage.h"

namespace Platform {
namespace {

} // namespace

MainWindow::MainWindow() : QMainWindow(),
posInited(false), trayIcon(0), trayIconMenu(0), icon256(qsl(":/gui/art/icon256.png")), iconbig256(icon256), wndIcon(QIcon::fromTheme("telegram", QIcon(QPixmap::fromImage(icon256, Qt::ColorOnly)))), _psCheckStatusIconLeft(100), _psLastIndicatorUpdate(0) {
	connect(&_psCheckStatusIconTimer, SIGNAL(timeout()), this, SLOT(psStatusIconCheck()));
	_psCheckStatusIconTimer.setSingleShot(false);

	connect(&_psUpdateIndicatorTimer, SIGNAL(timeout()), this, SLOT(psUpdateIndicator()));
	_psUpdateIndicatorTimer.setSingleShot(true);
}

bool MainWindow::psHasTrayIcon() const {
	return trayIcon || ((useAppIndicator || (useStatusIcon && trayIconChecked)) && (cWorkMode() != dbiwmWindowOnly));
}

void MainWindow::psStatusIconCheck() {
	_trayIconCheck(0);
	if (cSupportTray() || !--_psCheckStatusIconLeft) {
		_psCheckStatusIconTimer.stop();
		return;
	}
}

void MainWindow::psShowTrayMenu() {
}

void MainWindow::psRefreshTaskbarIcon() {
}

void MainWindow::psTrayMenuUpdated() {
	if (noQtTrayIcon && (useAppIndicator || useStatusIcon)) {
		const QList<QAction*> &actions = trayIconMenu->actions();
		if (_trayItems.isEmpty()) {
			DEBUG_LOG(("Creating tray menu!"));
			for (int32 i = 0, l = actions.size(); i != l; ++i) {
				GtkWidget *item = ps_gtk_menu_item_new_with_label(actions.at(i)->text().toUtf8());
				ps_gtk_menu_shell_append(PS_GTK_MENU_SHELL(_trayMenu), item);
				ps_g_signal_connect(item, "activate", G_CALLBACK(_trayMenuCallback), this);
				ps_gtk_widget_show(item);
				ps_gtk_widget_set_sensitive(item, actions.at(i)->isEnabled());

				_trayItems.push_back(qMakePair(item, actions.at(i)));
			}
		} else {
			DEBUG_LOG(("Updating tray menu!"));
			for (int32 i = 0, l = actions.size(); i != l; ++i) {
				if (i < _trayItems.size()) {
					ps_gtk_menu_item_set_label(reinterpret_cast<GtkMenuItem*>(_trayItems.at(i).first), actions.at(i)->text().toUtf8());
					ps_gtk_widget_set_sensitive(_trayItems.at(i).first, actions.at(i)->isEnabled());
				}
			}
		}
	}
}

void MainWindow::psSetupTrayIcon() {
	if (noQtTrayIcon) {
		if (!cSupportTray()) return;
		psUpdateCounter();
	} else {
		if (!trayIcon) {
			trayIcon = new QSystemTrayIcon(this);

			QIcon icon(QPixmap::fromImage(App::wnd()->iconLarge(), Qt::ColorOnly));

			trayIcon->setIcon(icon);
			trayIcon->setToolTip(str_const_toString(AppName));
			connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);
			connect(trayIcon, SIGNAL(messageClicked()), this, SLOT(showFromTray()));
			App::wnd()->updateTrayMenu();
		}
		psUpdateCounter();

		trayIcon->show();
		psUpdateDelegate();
	}
}

void MainWindow::psUpdateWorkmode() {
	if (!cSupportTray()) return;

	if (cWorkMode() == dbiwmWindowOnly) {
		if (noQtTrayIcon) {
			if (useAppIndicator) {
				ps_app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_PASSIVE);
			} else if (useStatusIcon) {
				ps_gtk_status_icon_set_visible(_trayIcon, false);
			}
		} else {
			if (trayIcon) {
				trayIcon->setContextMenu(0);
				trayIcon->deleteLater();
			}
			trayIcon = 0;
		}
	} else {
		if (noQtTrayIcon) {
			if (useAppIndicator) {
				ps_app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_ACTIVE);
			} else if (useStatusIcon) {
				ps_gtk_status_icon_set_visible(_trayIcon, true);
			}
		} else {
			psSetupTrayIcon();
		}
	}
}

void MainWindow::psUpdateIndicator() {
	_psUpdateIndicatorTimer.stop();
	_psLastIndicatorUpdate = getms();
	QFileInfo f(_trayIconImageFile());
	if (f.exists()) {
		QByteArray path = QFile::encodeName(f.absoluteFilePath()), name = QFile::encodeName(f.fileName());
		name = name.mid(0, name.size() - 4);
		ps_app_indicator_set_icon_full(_trayIndicator, path.constData(), name);
	} else {
		useAppIndicator = false;
	}
}

void MainWindow::psUpdateCounter() {
	setWindowIcon(wndIcon);

	int32 counter = App::histories().unreadBadge();

	setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
	if (_psUnityLauncherEntry) {
		if (counter > 0) {
			ps_unity_launcher_entry_set_count(_psUnityLauncherEntry, (counter > 9999) ? 9999 : counter);
			ps_unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, TRUE);
		} else {
			ps_unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, FALSE);
		}
	}

	if (noQtTrayIcon) {
		if (useAppIndicator) {
			if (getms() > _psLastIndicatorUpdate + 1000) {
				psUpdateIndicator();
			} else if (!_psUpdateIndicatorTimer.isActive()) {
				_psUpdateIndicatorTimer.start(100);
			}
		} else if (useStatusIcon && trayIconChecked) {
			loadPixbuf(_trayIconImageGen());
			ps_gtk_status_icon_set_from_pixbuf(_trayIcon, _trayPixbuf);
		}
	} else if (trayIcon) {
		int32 counter = App::histories().unreadBadge();
		bool muted = App::histories().unreadOnlyMuted();

		style::color bg = muted ? st::counterMuteBG : st::counterBG;
		QIcon iconSmall;
		iconSmall.addPixmap(QPixmap::fromImage(iconWithCounter(16, counter, bg, true), Qt::ColorOnly));
		iconSmall.addPixmap(QPixmap::fromImage(iconWithCounter(32, counter, bg, true), Qt::ColorOnly));
		trayIcon->setIcon(iconSmall);
	}
}

void MainWindow::psUpdateDelegate() {
}

void MainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		QList<QScreen*> screens = Application::screens();
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

void MainWindow::psInitFrameless() {
	psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));

	if (frameless) {
		//setWindowFlags(Qt::FramelessWindowHint);
	}
}

void MainWindow::psSavePosition(Qt::WindowState state) {
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
	QList<QScreen*> screens = Application::screens();
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
			Local::writeSettings();
		}
	}
}

void MainWindow::psUpdatedPosition() {
	psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

void MainWindow::psCreateTrayIcon() {
	if (!noQtTrayIcon) {
		cSetSupportTray(QSystemTrayIcon::isSystemTrayAvailable());
		return;
	}

	if (useAppIndicator) {
		DEBUG_LOG(("Trying to create AppIndicator"));
		_trayMenu = ps_gtk_menu_new();
		if (_trayMenu) {
			DEBUG_LOG(("Created gtk menu for appindicator!"));
			QFileInfo f(_trayIconImageFile());
			if (f.exists()) {
				QByteArray path = QFile::encodeName(f.absoluteFilePath());
				_trayIndicator = ps_app_indicator_new("Telegram Desktop", path.constData(), APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
				if (_trayIndicator) {
					DEBUG_LOG(("Created appindicator!"));
				} else {
					DEBUG_LOG(("Failed to app_indicator_new()!"));
				}
			} else {
				useAppIndicator = false;
				DEBUG_LOG(("Failed to create image file!"));
			}
		} else {
			DEBUG_LOG(("Failed to gtk_menu_new()!"));
		}
		if (_trayMenu && _trayIndicator) {
			ps_app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_ACTIVE);
			ps_app_indicator_set_menu(_trayIndicator, PS_GTK_MENU(_trayMenu));
			useStatusIcon = false;
		} else {
			DEBUG_LOG(("AppIndicator failed!"));
			useAppIndicator = false;
		}
	}
	if (useStatusIcon) {
		if (ps_gdk_init_check(0, 0)) {
			if (!_trayMenu) _trayMenu = ps_gtk_menu_new();
			if (_trayMenu) {
				loadPixbuf(_trayIconImageGen());
				_trayIcon = ps_gtk_status_icon_new_from_pixbuf(_trayPixbuf);
				if (_trayIcon) {
					ps_g_signal_connect(_trayIcon, "popup-menu", GCallback(_trayIconPopup), _trayMenu);
					ps_g_signal_connect(_trayIcon, "activate", GCallback(_trayIconActivate), _trayMenu);
					ps_g_signal_connect(_trayIcon, "size-changed", GCallback(_trayIconResized), _trayMenu);

					ps_gtk_status_icon_set_title(_trayIcon, "Telegram Desktop");
					ps_gtk_status_icon_set_tooltip_text(_trayIcon, "Telegram Desktop");
					ps_gtk_status_icon_set_visible(_trayIcon, true);
				} else {
					useStatusIcon = false;
				}
			} else {
				useStatusIcon = false;
			}
		} else {
			useStatusIcon = false;
		}
	}
	if (!useStatusIcon && !useAppIndicator) {
		if (_trayMenu) {
			ps_g_object_ref_sink(_trayMenu);
			ps_g_object_unref(_trayMenu);
			_trayMenu = 0;
		}
	}
	cSetSupportTray(useAppIndicator);
	if (useStatusIcon) {
		ps_g_idle_add((GSourceFunc)_trayIconCheck, 0);
		_psCheckStatusIconTimer.start(100);
	} else {
		psUpdateWorkmode();
	}
}

void MainWindow::psFirstShow() {
	psCreateTrayIcon();

	if (useUnityCount) {
		_psUnityLauncherEntry = ps_unity_launcher_entry_get_for_desktop_id("telegramdesktop.desktop");
		if (_psUnityLauncherEntry) {
			LOG(("Found Unity Launcher entry telegramdesktop.desktop!"));
		} else {
			_psUnityLauncherEntry = ps_unity_launcher_entry_get_for_desktop_id("Telegram.desktop");
			if (_psUnityLauncherEntry) {
				LOG(("Found Unity Launcher entry Telegram.desktop!"));
			} else {
				LOG(("Could not get Unity Launcher entry!"));
			}
		}
	} else {
		LOG(("Not using Unity Launcher count."));
	}

	finished = false;

	psUpdateMargins();

	bool showShadows = true;

	show();
	//_private.enableShadow(winId());
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if ((cLaunchMode() == LaunchModeAutoStart && cStartMinimized()) || cStartInTray()) {
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

	posInited = true;
}

bool MainWindow::psHandleTitle() {
	return false;
}

void MainWindow::psInitSysMenu() {
}

void MainWindow::psUpdateSysMenu(Qt::WindowState state) {
}

void MainWindow::psUpdateMargins() {
}

void MainWindow::psFlash() {
}

MainWindow::~MainWindow() {
	if (_trayIcon) {
		ps_g_object_unref(_trayIcon);
		_trayIcon = 0;
	}
	if (_trayPixbuf) {
		ps_g_object_unref(_trayPixbuf);
		_trayPixbuf = 0;
	}
	if (_trayMenu) {
		ps_g_object_ref_sink(_trayMenu);
		ps_g_object_unref(_trayMenu);
		_trayMenu = 0;
	}
	finished = true;
}

} // namespace Platform
