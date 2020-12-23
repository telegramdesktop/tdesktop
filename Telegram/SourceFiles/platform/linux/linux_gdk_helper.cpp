/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
#include "platform/linux/linux_gdk_helper.h"

#include "platform/linux/linux_libs.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_xcb_utilities_linux.h"

extern "C" {
#undef signals
#include <gdk/gdkx.h>
#define signals public
} // extern "C"

namespace Platform {
namespace internal {

enum class GtkLoaded {
	GtkNone,
	Gtk2,
	Gtk3,
};

GtkLoaded gdk_helper_loaded = GtkLoaded::GtkNone;

// To be able to compile with gtk-3.0 headers as well
#define GdkDrawable GdkWindow

// Gtk 2
using f_gdk_x11_drawable_get_xid = XID(*)(GdkDrawable*);
f_gdk_x11_drawable_get_xid gdk_x11_drawable_get_xid = nullptr;

// Gtk 3
using f_gdk_x11_window_get_type = GType (*)(void);
f_gdk_x11_window_get_type gdk_x11_window_get_type = nullptr;

// To be able to compile with gtk-2.0 headers as well
template <typename Object>
inline bool gdk_is_x11_window_check(Object *obj) {
	return Libs::g_type_cit_helper(obj, gdk_x11_window_get_type());
}

using f_gdk_x11_window_get_xid = Window(*)(GdkWindow *window);
f_gdk_x11_window_get_xid gdk_x11_window_get_xid = nullptr;

bool GdkHelperLoadGtk2(QLibrary &lib) {
#if defined DESKTOP_APP_USE_PACKAGED && !defined DESKTOP_APP_USE_PACKAGED_LAZY
	return false;
#else // DESKTOP_APP_USE_PACKAGED && !DESKTOP_APP_USE_PACKAGED_LAZY
	if (!LOAD_SYMBOL(lib, "gdk_x11_drawable_get_xid", gdk_x11_drawable_get_xid)) return false;
	return true;
#endif // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY
}

bool GdkHelperLoadGtk3(QLibrary &lib) {
	if (!LOAD_SYMBOL(lib, "gdk_x11_window_get_type", gdk_x11_window_get_type)) return false;
	if (!LOAD_SYMBOL(lib, "gdk_x11_window_get_xid", gdk_x11_window_get_xid)) return false;
	return true;
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
	return gdk_helper_loaded != GtkLoaded::GtkNone;
}

void XSetTransientForHint(GdkWindow *window, quintptr winId) {
	if (gdk_helper_loaded == GtkLoaded::Gtk2) {
		if (!IsWayland()) {
			xcb_change_property(
				base::Platform::XCB::GetConnectionFromQt(),
				XCB_PROP_MODE_REPLACE,
				gdk_x11_drawable_get_xid(window),
				XCB_ATOM_WM_TRANSIENT_FOR,
				XCB_ATOM_WINDOW,
				32,
				1,
				&winId);
		}
	} else if (gdk_helper_loaded == GtkLoaded::Gtk3) {
		if (!IsWayland() && gdk_is_x11_window_check(window)) {
			xcb_change_property(
				base::Platform::XCB::GetConnectionFromQt(),
				XCB_PROP_MODE_REPLACE,
				gdk_x11_window_get_xid(window),
				XCB_ATOM_WM_TRANSIENT_FOR,
				XCB_ATOM_WINDOW,
				32,
				1,
				&winId);
		}
	}
}

} // namespace internal
} // namespace Platform
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
