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
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "pspecific.h"

#include "lang.h"
#include "application.h"
#include "mainwidget.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

#undef signals
extern "C" {
    #include <libappindicator/app-indicator.h>
    #include <gtk/gtk.h>
}
#define signals public

#include <unity/unity/unity.h>

namespace {
	bool frameless = true;
	bool finished = true;
    bool useGtkBase = false, useAppIndicator = false, useStatusIcon = false, trayIconChecked = false, useUnityCount = false;

    AppIndicator *_trayIndicator = 0;
    GtkStatusIcon *_trayIcon = 0;
    GtkWidget *_trayMenu = 0;
    GdkPixbuf *_trayPixbuf = 0;
    QByteArray _trayPixbufData;
    QList<QPair<GtkWidget*, QObject*> > _trayItems;

    int32 _trayIconSize = 22;
    bool _trayIconMuted = true;
    int32 _trayIconCount = 0;
    QImage _trayIconImageBack, _trayIconImage;

    typedef gboolean (*f_gtk_init_check)(int *argc, char ***argv);
    f_gtk_init_check ps_gtk_init_check = 0;

    typedef GtkWidget* (*f_gtk_menu_new)(void);
    f_gtk_menu_new ps_gtk_menu_new = 0;

    typedef GType (*f_gtk_menu_get_type)(void) G_GNUC_CONST;
    f_gtk_menu_get_type ps_gtk_menu_get_type = 0;

    typedef GtkWidget* (*f_gtk_menu_item_new_with_label)(const gchar *label);
    f_gtk_menu_item_new_with_label ps_gtk_menu_item_new_with_label = 0;

    typedef void (*f_gtk_menu_item_set_label)(GtkMenuItem *menu_item, const gchar *label);
    f_gtk_menu_item_set_label ps_gtk_menu_item_set_label = 0;

    typedef void (*f_gtk_menu_shell_append)(GtkMenuShell *menu_shell, GtkWidget *child);
    f_gtk_menu_shell_append ps_gtk_menu_shell_append = 0;

    typedef GType (*f_gtk_menu_shell_get_type)(void) G_GNUC_CONST;
    f_gtk_menu_shell_get_type ps_gtk_menu_shell_get_type = 0;

    typedef void (*f_gtk_widget_show)(GtkWidget *widget);
    f_gtk_widget_show ps_gtk_widget_show = 0;

    typedef GtkWidget* (*f_gtk_widget_get_toplevel)(GtkWidget *widget);
    f_gtk_widget_get_toplevel ps_gtk_widget_get_toplevel = 0;

    typedef gboolean (*f_gtk_widget_get_visible)(GtkWidget *widget);
    f_gtk_widget_get_visible ps_gtk_widget_get_visible = 0;

    typedef void (*f_gtk_widget_set_sensitive)(GtkWidget *widget, gboolean sensitive);
    f_gtk_widget_set_sensitive ps_gtk_widget_set_sensitive = 0;

    typedef GTypeInstance* (*f_g_type_check_instance_cast)(GTypeInstance *instance, GType iface_type);
    f_g_type_check_instance_cast ps_g_type_check_instance_cast = 0;

#define _PS_G_TYPE_CIC(ip, gt, ct) ((ct*)ps_g_type_check_instance_cast((GTypeInstance*) ip, gt))
#define PS_G_TYPE_CHECK_INSTANCE_CAST(instance, g_type, c_type) (_PS_G_TYPE_CIC((instance), (g_type), c_type))
#define PS_GTK_TYPE_MENU (ps_gtk_menu_get_type())
#define PS_GTK_MENU(obj) (PS_G_TYPE_CHECK_INSTANCE_CAST((obj), PS_GTK_TYPE_MENU, GtkMenu))
#define PS_GTK_TYPE_MENU_SHELL (ps_gtk_menu_shell_get_type())
#define PS_GTK_MENU_SHELL(obj) (PS_G_TYPE_CHECK_INSTANCE_CAST((obj), PS_GTK_TYPE_MENU_SHELL, GtkMenuShell))

    typedef gulong (*f_g_signal_connect_data)(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data, GConnectFlags connect_flags);
    f_g_signal_connect_data ps_g_signal_connect_data = 0;

#define ps_g_signal_connect(instance, detailed_signal, c_handler, data) ps_g_signal_connect_data((instance), (detailed_signal), (c_handler), (data), NULL, (GConnectFlags)0)

    typedef AppIndicator* (*f_app_indicator_new)(const gchar *id, const gchar *icon_name, AppIndicatorCategory category);
    f_app_indicator_new ps_app_indicator_new = 0;

    typedef void (*f_app_indicator_set_status)(AppIndicator *self, AppIndicatorStatus status);
    f_app_indicator_set_status ps_app_indicator_set_status = 0;

    typedef void (*f_app_indicator_set_menu)(AppIndicator *self, GtkMenu *menu);
    f_app_indicator_set_menu ps_app_indicator_set_menu = 0;

    typedef void (*f_app_indicator_set_icon_full)(AppIndicator *self, const gchar *icon_name, const gchar *icon_desc);
    f_app_indicator_set_icon_full ps_app_indicator_set_icon_full = 0;

    typedef gboolean (*f_gdk_init_check)(gint *argc, gchar ***argv);
    f_gdk_init_check ps_gdk_init_check = 0;

    typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_data)(const guchar *data, GdkColorspace colorspace, gboolean has_alpha, int bits_per_sample, int width, int height, int rowstride, GdkPixbufDestroyNotify destroy_fn, gpointer destroy_fn_data);
    f_gdk_pixbuf_new_from_data ps_gdk_pixbuf_new_from_data = 0;

    typedef GtkStatusIcon* (*f_gtk_status_icon_new_from_pixbuf)(GdkPixbuf *pixbuf);
    f_gtk_status_icon_new_from_pixbuf ps_gtk_status_icon_new_from_pixbuf = 0;

    typedef void (*f_gtk_status_icon_set_from_pixbuf)(GtkStatusIcon *status_icon, GdkPixbuf *pixbuf);
    f_gtk_status_icon_set_from_pixbuf ps_gtk_status_icon_set_from_pixbuf = 0;

    typedef void (*f_gtk_status_icon_set_title)(GtkStatusIcon *status_icon, const gchar *title);
    f_gtk_status_icon_set_title ps_gtk_status_icon_set_title = 0;

    typedef void (*f_gtk_status_icon_set_tooltip_text)(GtkStatusIcon *status_icon, const gchar *title);
    f_gtk_status_icon_set_tooltip_text ps_gtk_status_icon_set_tooltip_text = 0;

    typedef void (*f_gtk_status_icon_set_visible)(GtkStatusIcon *status_icon, gboolean visible);
    f_gtk_status_icon_set_visible ps_gtk_status_icon_set_visible = 0;

    typedef gboolean (*f_gtk_status_icon_is_embedded)(GtkStatusIcon *status_icon);
    f_gtk_status_icon_is_embedded ps_gtk_status_icon_is_embedded = 0;

    typedef gboolean (*f_gtk_status_icon_get_geometry)(GtkStatusIcon *status_icon, GdkScreen **screen, GdkRectangle *area, GtkOrientation *orientation);
    f_gtk_status_icon_get_geometry ps_gtk_status_icon_get_geometry = 0;

    typedef void (*f_gtk_status_icon_position_menu)(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data);
    f_gtk_status_icon_position_menu ps_gtk_status_icon_position_menu = 0;

    typedef void (*f_gtk_menu_popup)(GtkMenu *menu, GtkWidget *parent_menu_shell, GtkWidget *parent_menu_item, GtkMenuPositionFunc func, gpointer data, guint button, guint32 activate_time);
    f_gtk_menu_popup ps_gtk_menu_popup = 0;

    typedef guint32 (*f_gtk_get_current_event_time)(void);
    f_gtk_get_current_event_time ps_gtk_get_current_event_time = 0;

    typedef gpointer (*f_g_object_ref_sink)(gpointer object);
    f_g_object_ref_sink ps_g_object_ref_sink = 0;

    typedef void (*f_g_object_unref)(gpointer object);
    f_g_object_unref ps_g_object_unref = 0;

    typedef guint (*f_g_idle_add)(GSourceFunc function, gpointer data);
    f_g_idle_add ps_g_idle_add = 0;

    typedef void (*f_unity_launcher_entry_set_count)(UnityLauncherEntry* self, gint64 value);
    f_unity_launcher_entry_set_count ps_unity_launcher_entry_set_count = 0;

    typedef void (*f_unity_launcher_entry_set_count_visible)(UnityLauncherEntry* self, gboolean value);
    f_unity_launcher_entry_set_count_visible ps_unity_launcher_entry_set_count_visible = 0;

    typedef UnityLauncherEntry* (*f_unity_launcher_entry_get_for_desktop_id)(const gchar* desktop_id);
    f_unity_launcher_entry_get_for_desktop_id ps_unity_launcher_entry_get_for_desktop_id = 0;

    QStringList _initLogs;

    template <typename TFunction>
    bool loadFunction(QLibrary &lib, const char *name, TFunction &func) {
        if (!lib.isLoaded()) return false;

        func = (TFunction)lib.resolve(name);
        if (func) {
            return true;
        } else {
            _initLogs.push_back(QString("Init Error: Failed to load '%1' function!").arg(name));
            return false;
        }
    }

    void _trayIconPopup(GtkStatusIcon *status_icon, guint button, guint32 activate_time, gpointer popup_menu) {
        ps_gtk_menu_popup(PS_GTK_MENU(popup_menu), NULL, NULL, ps_gtk_status_icon_position_menu, status_icon, button, activate_time);
    }

    void _trayIconActivate(GtkStatusIcon *status_icon, gpointer popup_menu) {
        if (App::wnd()->isActiveWindow() && App::wnd()->isVisible()) {
            ps_gtk_menu_popup(PS_GTK_MENU(popup_menu), NULL, NULL, ps_gtk_status_icon_position_menu, status_icon, 0, ps_gtk_get_current_event_time());
        } else {
            App::wnd()->showFromTray();
        }
    }

    gboolean _trayIconResized(GtkStatusIcon *status_icon, gint size, gpointer popup_menu) {
       _trayIconSize = size;
        if (App::wnd()) App::wnd()->psUpdateCounter();
        return FALSE;
    }

#if Q_BYTE_ORDER == Q_BIG_ENDIAN

#define QT_RED 3
#define QT_GREEN 2
#define QT_BLUE 1
#define QT_ALPHA 0

#else

#define QT_RED 0
#define QT_GREEN 1
#define QT_BLUE 2
#define QT_ALPHA 3

#endif

#define GTK_RED 2
#define GTK_GREEN 1
#define GTK_BLUE 0
#define GTK_ALPHA 3

    QImage _trayIconImageGen() {
        int32 counter = App::histories().unreadFull, counterSlice = (counter >= 1000) ? (1000 + (counter % 100)) : counter;
        bool muted = (App::histories().unreadMuted >= counter);
        if (_trayIconImage.isNull() || _trayIconImage.width() != _trayIconSize || muted != _trayIconMuted || counterSlice != _trayIconCount) {
            if (_trayIconImageBack.isNull() || _trayIconImageBack.width() != _trayIconSize) {
                _trayIconImageBack = App::wnd()->iconLarge().scaled(_trayIconSize, _trayIconSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                _trayIconImageBack = _trayIconImageBack.convertToFormat(QImage::Format_ARGB32);
                int w = _trayIconImageBack.width(), h = _trayIconImageBack.height(), perline = _trayIconImageBack.bytesPerLine();
                uchar *bytes = _trayIconImageBack.bits();
                for (int32 y = 0; y < h; ++y) {
                    for (int32 x = 0; x < w; ++x) {
                        int32 srcoff = y * perline + x * 4;
                        bytes[srcoff + QT_RED  ] = qMax(bytes[srcoff + QT_RED  ], uchar(224));
                        bytes[srcoff + QT_GREEN] = qMax(bytes[srcoff + QT_GREEN], uchar(165));
                        bytes[srcoff + QT_BLUE ] = qMax(bytes[srcoff + QT_BLUE ], uchar(44));
                    }
                }
            }
            _trayIconImage = _trayIconImageBack;
            if (counter > 0) {
                QPainter p(&_trayIconImage);
                int32 layerSize = -16;
                if (_trayIconSize >= 48) {
                    layerSize = -32;
                } else if (_trayIconSize >= 36) {
                    layerSize = -24;
                } else if (_trayIconSize >= 32) {
                    layerSize = -20;
                }
                QImage layer = App::wnd()->iconWithCounter(layerSize, counter, (muted ? st::counterMuteBG : st::counterBG), false);
                p.drawImage(_trayIconImage.width() - layer.width() - 1, _trayIconImage.height() - layer.height() - 1, layer);
            }
        }
        return _trayIconImage;
    }

    QString _trayIconImageFile() {
        int32 counter = App::histories().unreadFull, counterSlice = (counter >= 1000) ? (1000 + (counter % 100)) : counter;
        bool muted = (App::histories().unreadMuted >= counter);

        QString name = cWorkingDir() + qsl("tdata/ticons/ico%1_%2_%3.png").arg(muted ? "mute" : "").arg(_trayIconSize).arg(counterSlice);
        QFileInfo info(name);
        if (info.exists()) return name;

        QImage img = _trayIconImageGen();
        if (img.save(name, "PNG")) return name;

        QDir dir(info.absoluteDir());
        if (!dir.exists()) {
            dir.mkpath(dir.absolutePath());
            if (img.save(name, "PNG")) return name;
        }

        return QString();
    }

    void loadPixbuf(QImage image) {
        int w = image.width(), h = image.height(), perline = image.bytesPerLine(), s = image.byteCount();
        _trayPixbufData.resize(w * h * 4);
        uchar *result = (uchar*)_trayPixbufData.data(), *bytes = image.bits();
        for (int32 y = 0; y < h; ++y) {
            for (int32 x = 0; x < w; ++x) {
                int32 offset = (y * w + x) * 4, srcoff = y * perline + x * 4;
                result[offset + GTK_RED  ] = bytes[srcoff + QT_RED  ];
                result[offset + GTK_GREEN] = bytes[srcoff + QT_GREEN];
                result[offset + GTK_BLUE ] = bytes[srcoff + QT_BLUE ];
                result[offset + GTK_ALPHA] = bytes[srcoff + QT_ALPHA];
            }
        }

        if (_trayPixbuf) ps_g_object_unref(_trayPixbuf);
        _trayPixbuf = ps_gdk_pixbuf_new_from_data(result, GDK_COLORSPACE_RGB, true, 8, w, h, w * 4, 0, 0);
    }

    void _trayMenuCallback(GtkMenu *menu, gpointer data) {
        for (int32 i = 0, l = _trayItems.size(); i < l; ++i) {
            if ((void*)_trayItems.at(i).first == (void*)menu) {
                QMetaObject::invokeMethod(_trayItems.at(i).second, "triggered");
            }
        }
    }

    static gboolean _trayIconCheck(gpointer/* pIn*/) {
        if (useStatusIcon && !trayIconChecked) {
            if (ps_gtk_status_icon_is_embedded(_trayIcon)) {
                trayIconChecked = true;
                cSetSupportTray(true);
                if (App::wnd()) {
                    App::wnd()->psUpdateWorkmode();
                    App::wnd()->psUpdateCounter();
                    App::wnd()->updateTrayMenu();
                }
            }
        }
        return FALSE;
    }

    class _PsInitializer {
    public:
        _PsInitializer() {
            setupGtk();
            setupUnity();
        }

        bool loadLibrary(QLibrary &lib, const char *name, int version) {
            lib.setFileNameAndVersion(QLatin1String(name), version);
            if (lib.load()) {
                _initLogs.push_back(QString("Loaded '%1' version %2 library").arg(name).arg(version));
                return true;
            }
            lib.setFileNameAndVersion(QLatin1String(name), QString());
            if (lib.load()) {
                _initLogs.push_back(QString("Loaded '%1' without version library").arg(name));
                return true;
            }
            return false;
        }

        void setupGtkBase(QLibrary &lib_gtk) {
            if (!loadFunction(lib_gtk, "gtk_init_check", ps_gtk_init_check)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_new", ps_gtk_menu_new)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_get_type", ps_gtk_menu_get_type)) return;

            if (!loadFunction(lib_gtk, "gtk_menu_item_new_with_label", ps_gtk_menu_item_new_with_label)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_item_set_label", ps_gtk_menu_item_set_label)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_shell_append", ps_gtk_menu_shell_append)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_shell_get_type", ps_gtk_menu_shell_get_type)) return;
            if (!loadFunction(lib_gtk, "gtk_widget_show", ps_gtk_widget_show)) return;
            if (!loadFunction(lib_gtk, "gtk_widget_get_toplevel", ps_gtk_widget_get_toplevel)) return;
            if (!loadFunction(lib_gtk, "gtk_widget_get_visible", ps_gtk_widget_get_visible)) return;
            if (!loadFunction(lib_gtk, "gtk_widget_set_sensitive", ps_gtk_widget_set_sensitive)) return;

            if (!loadFunction(lib_gtk, "g_type_check_instance_cast", ps_g_type_check_instance_cast)) return;
            if (!loadFunction(lib_gtk, "g_signal_connect_data", ps_g_signal_connect_data)) return;

            useGtkBase = true;
        }

        void setupAppIndicator(QLibrary &lib_indicator) {
            if (!loadFunction(lib_indicator, "app_indicator_new", ps_app_indicator_new)) return;
            if (!loadFunction(lib_indicator, "app_indicator_set_status", ps_app_indicator_set_status)) return;
            if (!loadFunction(lib_indicator, "app_indicator_set_menu", ps_app_indicator_set_menu)) return;
            if (!loadFunction(lib_indicator, "app_indicator_set_icon_full", ps_app_indicator_set_icon_full)) return;
            useAppIndicator = true;
        }

        void setupGtk() {
            QLibrary lib_gtk, lib_indicator;
            if (loadLibrary(lib_gtk, "gtk-3", 0)) {
                if (loadLibrary(lib_indicator, "appindicator3", 1)) {
                    setupGtkBase(lib_gtk);
                    setupAppIndicator(lib_indicator);
                }
            }
            if (!useGtkBase || !useAppIndicator) {
                if (lib_gtk.isLoaded()) lib_gtk.unload();
                if (lib_indicator.isLoaded()) lib_indicator.unload();
                if (loadLibrary(lib_gtk, "gtk-x11-2.0", 0)) {
                    if (loadLibrary(lib_indicator, "appindicator", 1)) {
                        useGtkBase = useAppIndicator = false;
                        setupGtkBase(lib_gtk);
                        setupAppIndicator(lib_indicator);
                    }
                }
            }
            if (!useGtkBase) {
                useAppIndicator = false;
                _initLogs.push_back(QString("Init Error: Failed to load 'gtk-x11-2.0' library!"));
                return;
            }

            if (!loadFunction(lib_gtk, "gdk_init_check", ps_gdk_init_check)) return;
            if (!loadFunction(lib_gtk, "gdk_pixbuf_new_from_data", ps_gdk_pixbuf_new_from_data)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_new_from_pixbuf", ps_gtk_status_icon_new_from_pixbuf)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_set_from_pixbuf", ps_gtk_status_icon_set_from_pixbuf)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_set_title", ps_gtk_status_icon_set_title)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_set_tooltip_text", ps_gtk_status_icon_set_tooltip_text)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_set_visible", ps_gtk_status_icon_set_visible)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_is_embedded", ps_gtk_status_icon_is_embedded)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_get_geometry", ps_gtk_status_icon_get_geometry)) return;
            if (!loadFunction(lib_gtk, "gtk_status_icon_position_menu", ps_gtk_status_icon_position_menu)) return;
            if (!loadFunction(lib_gtk, "gtk_menu_popup", ps_gtk_menu_popup)) return;
            if (!loadFunction(lib_gtk, "gtk_get_current_event_time", ps_gtk_get_current_event_time)) return;
            if (!loadFunction(lib_gtk, "g_object_ref_sink", ps_g_object_ref_sink)) return;
            if (!loadFunction(lib_gtk, "g_object_unref", ps_g_object_unref)) return;
            if (!loadFunction(lib_gtk, "g_idle_add", ps_g_idle_add)) return;
            useStatusIcon = true;
        }

        void setupUnity() {
            QLibrary lib_unity(QLatin1String("unity"), 9, 0);
            if (!loadLibrary(lib_unity, "unity", 9)) return;

            if (!loadFunction(lib_unity, "unity_launcher_entry_get_for_desktop_id", ps_unity_launcher_entry_get_for_desktop_id)) return;
            if (!loadFunction(lib_unity, "unity_launcher_entry_set_count", ps_unity_launcher_entry_set_count)) return;
            if (!loadFunction(lib_unity, "unity_launcher_entry_set_count_visible", ps_unity_launcher_entry_set_count_visible)) return;
            useUnityCount = true;
        }
    };
    _PsInitializer _psInitializer;

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

    UnityLauncherEntry *_psUnityLauncherEntry = 0;
};

PsMainWindow::PsMainWindow(QWidget *parent) : QMainWindow(parent),
posInited(false), trayIcon(0), trayIconMenu(0), icon256(qsl(":/gui/art/icon256.png")), iconbig256(icon256), wndIcon(QPixmap::fromImage(icon256, Qt::ColorOnly)), _psCheckStatusIconLeft(100), _psLastIndicatorUpdate(0) {
    connect(&psIdleTimer, SIGNAL(timeout()), this, SLOT(psIdleTimeout()));
    psIdleTimer.setSingleShot(false);

    connect(&_psCheckStatusIconTimer, SIGNAL(timeout()), this, SLOT(psStatusIconCheck()));
    _psCheckStatusIconTimer.setSingleShot(false);

    connect(&_psUpdateIndicatorTimer, SIGNAL(timeout()), this, SLOT(psUpdateIndicator()));
    _psUpdateIndicatorTimer.setSingleShot(true);
}

void PsMainWindow::psNotIdle() const {
	psIdleTimer.stop();
	if (psIdle) {
		psIdle = false;
		if (App::main()) App::main()->setOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	}
}

bool PsMainWindow::psHasTrayIcon() const {
    return trayIcon || ((useAppIndicator || (useStatusIcon && trayIconChecked)) && (cWorkMode() != dbiwmWindowOnly));
}

void PsMainWindow::psStatusIconCheck() {
    _trayIconCheck(0);
    if (cSupportTray() || !--_psCheckStatusIconLeft) {
        _psCheckStatusIconTimer.stop();
        return;
    }
}

void PsMainWindow::psIdleTimeout() {
    int64 idleTime = 0;//objc_idleTime();
    if (idleTime >= 0) {
        if (idleTime <= IdleMsecs) {
			psNotIdle();
		}
    } else { // error
		psNotIdle();
	}
}

void PsMainWindow::psShowTrayMenu() {
}

bool PsMainWindow::psIsOnline(int state) const {
	if (state < 0) state = this->windowState();
	if (state & Qt::WindowMinimized) {
		return false;
	} else if (!isVisible()) {
		return false;
	}
    int64 idleTime = 0;//objc_idleTime();
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

void PsMainWindow::psTrayMenuUpdated() {
    if (useAppIndicator || useStatusIcon) {
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

void PsMainWindow::psSetupTrayIcon() {
    if (!cSupportTray()) return;
    psUpdateCounter();
}

void PsMainWindow::psUpdateWorkmode() {
    if (!cSupportTray()) return;

    if (cWorkMode() == dbiwmWindowOnly) {
        if (useAppIndicator) {
            ps_app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_PASSIVE);
        } else if (useStatusIcon) {
            ps_gtk_status_icon_set_visible(_trayIcon, false);
        }
    } else {
        if (useAppIndicator) {
            ps_app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_ACTIVE);
        } else if (useStatusIcon) {
            ps_gtk_status_icon_set_visible(_trayIcon, true);
        }
    }
    setWindowIcon(wndIcon);
}

void PsMainWindow::psUpdateIndicator() {
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

void PsMainWindow::psUpdateCounter() {
    setWindowIcon(wndIcon);

	int32 counter = App::histories().unreadFull;

    setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
    if (_psUnityLauncherEntry) {
        if (counter > 0) {
            ps_unity_launcher_entry_set_count(_psUnityLauncherEntry, (counter > 9999) ? 9999 : counter);
            ps_unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, TRUE);
        } else {
            ps_unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, FALSE);
        }
    }

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
}

void PsMainWindow::psUpdateDelegate() {
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
    psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

void PsMainWindow::psStateChanged(Qt::WindowState state) {
	psUpdateSysMenu(state);
	psUpdateMargins();
    psSavePosition(state);
}

void PsMainWindow::psCreateTrayIcon() {
    if (useAppIndicator) {
        DEBUG_LOG(("Trying to create AppIndicator"));
        if (ps_gtk_init_check(0, 0)) {
            DEBUG_LOG(("Checked gtk with gtk_init_check!"));
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
        } else {
            DEBUG_LOG(("Failed to gtk_init_check(0, 0)!"));
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
        if (ps_gtk_init_check(0, 0) && ps_gdk_init_check(0, 0)) {
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

void PsMainWindow::psFirstShow() {
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
    //_private.startBounce();
}

PsMainWindow::~PsMainWindow() {
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

namespace {
    QRect _monitorRect;
    uint64 _monitorLastGot = 0;
}

QRect psDesktopRect() {
    uint64 tnow = getms();
    if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
        _monitorLastGot = tnow;
        _monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
    }
    return _monitorRect;
}

void psShowOverAll(QWidget *w, bool canFocus) {
    w->show();
}

void psBringToBack(QWidget *w) {
    w->hide();
}

void PsMainWindow::psActivateNotify(NotifyWindow *w) {
}

void PsMainWindow::psClearNotifies(PeerId peerId) {
}

void PsMainWindow::psNotifyShown(NotifyWindow *w) {
}

void PsMainWindow::psPlatformNotify(HistoryItem *item) {
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

bool _removeDirectory(const QString &path) { // from http://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
    QByteArray pathRaw = QFile::encodeName(path);
    DIR *d = opendir(pathRaw.constData());
    if (!d) return false;

    while (struct dirent *p = readdir(d)) {
        /* Skip the names "." and ".." as we don't want to recurse on them. */
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

        QString fname = path + '/' + p->d_name;
        QByteArray fnameRaw = QFile::encodeName(fname);
        struct stat statbuf;
        if (!stat(fnameRaw.constData(), &statbuf)) {
            if (S_ISDIR(statbuf.st_mode)) {
                if (!_removeDirectory(fname)) {
                    closedir(d);
                    return false;
                }
            } else {
                if (unlink(fnameRaw.constData())) {
                    closedir(d);
                    return false;
                }
            }
        }
    }
    closedir(d);

    return !rmdir(pathRaw.constData());
}

void PsUpdateDownloader::deleteDir(const QString &dir) {
    _removeDirectory(dir);
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
        if (int32(version) <= AppVersion) {
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


QStringList psInitLogs() {
    return _initLogs;
}

void psClearInitLogs() {
    _initLogs = QStringList();
}

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

QString psCurrentCountry() {
    QString country;// = objc_currentCountry();
	return country.isEmpty() ? QString::fromLatin1(DefaultCountry) : country;
}

QString psCurrentLanguage() {
    QString lng;// = objc_currentLang();
	return lng.isEmpty() ? QString::fromLatin1(DefaultLanguage) : lng;
}

namespace {
    QString _psHomeDir() {
        struct passwd *pw = getpwuid(getuid());
        return (pw && pw->pw_dir && strlen(pw->pw_dir)) ? (QString::fromLocal8Bit(pw->pw_dir) + '/') : QString();
    }
}

QString psAppDataPath() {
    QString home(_psHomeDir());
    return home.isEmpty() ? QString() : (home + qsl(".TelegramDesktop/"));
}

QString psDownloadPath() {
	return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + '/' + QString::fromWCharArray(AppName) + '/';
}

QString psCurrentExeDirectory(int argc, char *argv[]) {
    QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
    if (!first.isEmpty()) {
        QFileInfo info(first);
        if (info.isSymLink()) {
            info = info.symLinkTarget();
        }
        if (info.exists()) {
            return QDir(info.absolutePath()).absolutePath() + '/';
        }
    }
	return QString();
}

QString psCurrentExeName(int argc, char *argv[]) {
	QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
	if (!first.isEmpty()) {
		QFileInfo info(first);
        if (info.isSymLink()) {
            info = info.symLinkTarget();
        }
        if (info.exists()) {
			return info.fileName();
		}
	}
	return QString();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
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

#ifdef Q_OS_LINUX
bool moveFile(const char *from, const char *to) {
    FILE *ffrom = fopen(from, "rb"), *fto = fopen(to, "wb");
    if (!ffrom) {
        if (fto) fclose(fto);
        return false;
    }
    if (!fto) {
        fclose(ffrom);
        return false;
    }
    static const int BufSize = 65536;
    char buf[BufSize];
    while (size_t size = fread(buf, 1, BufSize, ffrom)) {
        fwrite(buf, 1, size, fto);
    }

    struct stat fst; // from http://stackoverflow.com/questions/5486774/keeping-fileowner-and-permissions-after-copying-file-in-c
    //let's say this wont fail since you already worked OK on that fp
    if (fstat(fileno(ffrom), &fst) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }
    //update to the same uid/gid
    if (fchown(fileno(fto), fst.st_uid, fst.st_gid) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }
    //update the permissions
    if (fchmod(fileno(fto), fst.st_mode) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }

    fclose(ffrom);
    fclose(fto);

    if (unlink(from)) {
        return false;
    }

    return true;
}
#endif

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
#elif defined Q_OS_LINUX
    QString curUpdater = (cExeDir() + "Updater");
    QFileInfo updater(cWorkingDir() + "tupdates/ready/Updater");
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
#elif defined Q_OS_LINUX
    if (!moveFile(QFile::encodeName(updater.absoluteFilePath()).constData(), QFile::encodeName(curUpdater).constData())) {
        PsUpdateDownloader::clearAll();
        return false;
    }
#endif
    return true;
}

void psPostprocessFile(const QString &name) {
}

void psOpenFile(const QString &name, bool openWith) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(name));
}

void psShowInFolder(const QString &name) {
    App::wnd()->layerHidden();
    system(("nautilus \"" + QFileInfo(name).absoluteDir().absolutePath() + "\"").toLocal8Bit().constData());
}

void psStart() {
}

void psFinish() {
}

namespace {
    bool _psRunCommand(const QString &command) {
        int result = system(command.toLocal8Bit().constData());
        if (result) {
            DEBUG_LOG(("App Error: command failed, code: %1, command: %2").arg(result).arg(command.toLocal8Bit().constData()));
            return false;
        }
        DEBUG_LOG(("App Info: command succeeded, command: %1").arg(command.toLocal8Bit().constData()));
        return true;
    }
}

void psRegisterCustomScheme() {
    QString home(_psHomeDir());
    if (home.isEmpty()) return;

    DEBUG_LOG(("App Info: placing .desktop file"));
    if (QDir(home + qsl(".local/")).exists()) {
        QString apps = home + qsl(".local/share/applications/");
        if (!QDir(apps).exists()) QDir().mkpath(apps);

        QString path = cWorkingDir() + qsl("tdata/"), file = path + qsl("telegramdesktop.desktop");
        QDir().mkpath(path);
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            QString icon = path + qsl("icon.png");
            if (!QFile(icon).exists()) {
                if (QFile(qsl(":/gui/art/icon256.png")).copy(icon)) {
                    DEBUG_LOG(("App Info: Icon copied to 'tdata'"));
                }

            }

            QTextStream s(&f);
            s.setCodec("UTF-8");
            s << "[Desktop Entry]\n";
            s << "Encoding=UTF-8\n";
            s << "Version=1.0\n";
            s << "Name=Telegram Desktop\n";
            s << "Comment=Official desktop version of Telegram messaging app\n";
            s << "Exec=" << cExeDir().toLocal8Bit().constData() << cExeName().toLocal8Bit().constData() << " -- %u\n";
            s << "Icon=" << icon.toLocal8Bit().constData() << "\n";
            s << "Terminal=false\n";
            s << "Type=Application\n";
            s << "Categories=Network;\n";
            s << "MimeType=application/x-xdg-protocol-tg;x-scheme-handler/tg;\n";
            f.close();

            if (_psRunCommand(qsl("desktop-file-install --dir=%1.local/share/applications --delete-original \"%2\"").arg(home).arg(file))) {
                DEBUG_LOG(("App Info: removing old .desktop file"));
                QFile(qsl("%1.local/share/applications/telegram.desktop").arg(home)).remove();

                _psRunCommand(qsl("update-desktop-database %1.local/share/applications").arg(home));
                _psRunCommand(qsl("xdg-mime default telegramdesktop.desktop x-scheme-handler/tg"));
            }
        } else {
            LOG(("App Error: Could not open '%1' for write").arg(file));
        }
    }

    DEBUG_LOG(("App Info: registerting for Gnome"));
    if (_psRunCommand(qsl("gconftool-2 -t string -s /desktop/gnome/url-handlers/tg/command \"%1 -- %s\"").arg(cExeDir() + cExeName()))) {
        _psRunCommand(qsl("gconftool-2 -t bool -s /desktop/gnome/url-handlers/tg/needs_terminal false"));
        _psRunCommand(qsl("gconftool-2 -t bool -s /desktop/gnome/url-handlers/tg/enabled true"));
    }

    DEBUG_LOG(("App Info: placing .protocol file"));
    QString services;
    if (QDir(home + qsl(".kde4/")).exists()) {
        services = home + qsl(".kde4/share/kde4/services/");
    } else if (QDir(home + qsl(".kde/")).exists()) {
        services = home + qsl(".kde/share/kde4/services/");
    }
    if (!services.isEmpty()) {
        if (!QDir(services).exists()) QDir().mkpath(services);

        QString path = services, file = path + qsl("tg.protocol");
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            QTextStream s(&f);
            s.setCodec("UTF-8");
            s << "[Protocol]\n";
            s << "exec=" << cExeDir().toLocal8Bit().constData() << cExeName().toLocal8Bit().constData() << " -- %u\n";
            s << "protocol=tg\n";
            s << "input=none\n";
            s << "output=none\n";
            s << "helper=true\n";
            s << "listing=false\n";
            s << "reading=false\n";
            s << "writing=false\n";
            s << "makedir=false\n";
            s << "deleting=false\n";
            f.close();
        } else {
            LOG(("App Error: Could not open '%1' for write").arg(file));
        }
    }
}

bool _execUpdater(bool update = true) {
    static const int MaxLen = 65536, MaxArgsCount = 128;

    char path[MaxLen] = {0};
    QByteArray data(QFile::encodeName(cExeDir() + "Updater"));
    memcpy(path, data.constData(), data.size());

    char *args[MaxArgsCount] = {0}, p_noupdate[] = "-noupdate", p_autostart[] = "-autostart", p_debug[] = "-debug", p_tosettings[] = "-tosettings", p_key[] = "-key", p_path[] = "-workpath";
    char p_datafile[MaxLen] = {0}, p_pathbuf[MaxLen] = {0};
    int argIndex = 0;
    args[argIndex++] = path;
    if (!update) {
        args[argIndex++] = p_noupdate;
        args[argIndex++] = p_tosettings;
    }
    if (cFromAutoStart()) args[argIndex++] = p_autostart;
    if (cDebug()) args[argIndex++] = p_debug;
    if (cDataFile() != (cTestMode() ? qsl("data_test") : qsl("data"))) {
        QByteArray dataf = QFile::encodeName(cDataFile());
        if (dataf.size() < MaxLen) {
            memcpy(p_datafile, dataf.constData(), dataf.size());
            args[argIndex++] = p_key;
            args[argIndex++] = p_datafile;
        }
    }
    QByteArray pathf = cWorkingDir().toLocal8Bit();
    if (pathf.size() < MaxLen) {
        memcpy(p_pathbuf, pathf.constData(), pathf.size());
        args[argIndex++] = p_path;
        args[argIndex++] = p_pathbuf;
    }

    pid_t pid = fork();
    switch (pid) {
    case -1: return false;
    case 0: execv(path, args); return false;
    }
    return true;
}

void psExecUpdater() {
    if (!_execUpdater()) {
		QString readyPath = cWorkingDir() + qsl("tupdates/ready");
		PsUpdateDownloader::deleteDir(readyPath);
	}
}

void psExecTelegram() {
    _execUpdater(false);
}

void psAutoStart(bool start, bool silent) {
}

void psSendToMenu(bool send, bool silent) {
}

void psUpdateOverlayed(QWidget *widget) {
}
