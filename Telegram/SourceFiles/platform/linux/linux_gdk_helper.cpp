/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gdk_helper.h"

#include "base/platform/linux/base_linux_gtk_integration_p.h"
#include "platform/linux/linux_gtk_integration_p.h"

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
extern "C" {
#include <gdk/gdkx.h>
} // extern "C"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

namespace Platform {
namespace internal {

using namespace Platform::Gtk;

enum class GtkLoaded {
	GtkNone,
	Gtk2,
	Gtk3,
};

GtkLoaded gdk_helper_loaded = GtkLoaded::GtkNone;

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
// To be able to compile with gtk-3.0 headers as well
#define GdkDrawable GdkWindow

// Gtk 2
using f_gdk_x11_drawable_get_xdisplay = Display*(*)(GdkDrawable*);
f_gdk_x11_drawable_get_xdisplay gdk_x11_drawable_get_xdisplay = nullptr;

using f_gdk_x11_drawable_get_xid = XID(*)(GdkDrawable*);
f_gdk_x11_drawable_get_xid gdk_x11_drawable_get_xid = nullptr;

// Gtk 3
using f_gdk_x11_window_get_type = GType (*)(void);
f_gdk_x11_window_get_type gdk_x11_window_get_type = nullptr;

// To be able to compile with gtk-2.0 headers as well
template <typename Object>
inline bool gdk_is_x11_window_check(Object *obj) {
	return g_type_cit_helper(obj, gdk_x11_window_get_type());
}

using f_gdk_window_get_display = GdkDisplay*(*)(GdkWindow *window);
f_gdk_window_get_display gdk_window_get_display = nullptr;

using f_gdk_x11_display_get_xdisplay = Display*(*)(GdkDisplay *display);
f_gdk_x11_display_get_xdisplay gdk_x11_display_get_xdisplay = nullptr;

using f_gdk_x11_window_get_xid = Window(*)(GdkWindow *window);
f_gdk_x11_window_get_xid gdk_x11_window_get_xid = nullptr;
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

bool GdkHelperLoadGtk2(QLibrary &lib) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#ifdef LINK_TO_GTK
	return false;
#else // LINK_TO_GTK
	if (!LOAD_GTK_SYMBOL(lib, "gdk_x11_drawable_get_xdisplay", gdk_x11_drawable_get_xdisplay)) return false;
	if (!LOAD_GTK_SYMBOL(lib, "gdk_x11_drawable_get_xid", gdk_x11_drawable_get_xid)) return false;
	return true;
#endif // !LINK_TO_GTK
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
	return false;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
}

bool GdkHelperLoadGtk3(QLibrary &lib) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (!LOAD_GTK_SYMBOL(lib, "gdk_x11_window_get_type", gdk_x11_window_get_type)) return false;
	if (!LOAD_GTK_SYMBOL(lib, "gdk_window_get_display", gdk_window_get_display)) return false;
	if (!LOAD_GTK_SYMBOL(lib, "gdk_x11_display_get_xdisplay", gdk_x11_display_get_xdisplay)) return false;
	if (!LOAD_GTK_SYMBOL(lib, "gdk_x11_window_get_xid", gdk_x11_window_get_xid)) return false;
	return true;
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
	return false;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
}

void GdkHelperLoad(QLibrary &lib) {
	gdk_helper_loaded = GtkLoaded::GtkNone;
	if (GdkHelperLoadGtk3(lib)) {
		gdk_helper_loaded = GtkLoaded::Gtk3;
	} else if (GdkHelperLoadGtk2(lib)) {
		gdk_helper_loaded = GtkLoaded::Gtk2;
	}
}

bool GdkHelperLoaded() {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	return gdk_helper_loaded != GtkLoaded::GtkNone;
#else // !DESKTOP_APP_DISABLE_X11_INTEGRATION
	return true;
#endif // DESKTOP_APP_DISABLE_X11_INTEGRATION
}

void XSetTransientForHint(GdkWindow *window, quintptr winId) {
#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (gdk_helper_loaded == GtkLoaded::Gtk2) {
		::XSetTransientForHint(gdk_x11_drawable_get_xdisplay(window),
							   gdk_x11_drawable_get_xid(window),
							   winId);
	} else if (gdk_helper_loaded == GtkLoaded::Gtk3) {
		if (gdk_is_x11_window_check(window)) {
			::XSetTransientForHint(gdk_x11_display_get_xdisplay(gdk_window_get_display(window)),
								   gdk_x11_window_get_xid(window),
								   winId);
		}
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

} // namespace internal
} // namespace Platform
