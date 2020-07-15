/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_libs.h"

#include "base/platform/base_platform_info.h"
#include "platform/linux/linux_xlib_helper.h"
#include "platform/linux/linux_gdk_helper.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/specific_linux.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "mainwindow.h"

namespace Platform {
namespace Libs {
namespace {

bool gtkTriedToInit = false;

bool loadLibrary(QLibrary &lib, const char *name, int version) {
#if defined DESKTOP_APP_USE_PACKAGED && !defined DESKTOP_APP_USE_PACKAGED_LAZY
	return true;
#else // DESKTOP_APP_USE_PACKAGED && !DESKTOP_APP_USE_PACKAGED_LAZY
	DEBUG_LOG(("Loading '%1' with version %2...").arg(QLatin1String(name)).arg(version));
	lib.setFileNameAndVersion(QLatin1String(name), version);
	if (lib.load()) {
		DEBUG_LOG(("Loaded '%1' with version %2!").arg(QLatin1String(name)).arg(version));
		return true;
	}
	lib.setFileNameAndVersion(QLatin1String(name), QString());
	if (lib.load()) {
		DEBUG_LOG(("Loaded '%1' without version!").arg(QLatin1String(name)));
		return true;
	}
	LOG(("Could not load '%1' with version %2 :(").arg(QLatin1String(name)).arg(version));
	return false;
#endif // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY
}

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
template <typename T>
T gtkSetting(const gchar *propertyName) {
	GtkSettings *settings = gtk_settings_get_default();
	T value;
	g_object_get(settings, propertyName, &value, nullptr);
	return value;
}

QString gtkSetting(const gchar *propertyName) {
	gchararray value = gtkSetting<gchararray>(propertyName);
	QString str = QString::fromUtf8(value);
	g_free(value);
	return str;
}

void gtkMessageHandler(
		const gchar *log_domain,
		GLogLevelFlags log_level,
		const gchar *message,
		gpointer unused_data) {
	// Silence false-positive Gtk warnings (we are using Xlib to set
	// the WM_TRANSIENT_FOR hint).
	if (message != qstr("GtkDialog mapped without a transient parent. "
		"This is discouraged.")) {
		// For other messages, call the default handler.
		g_log_default_handler(log_domain, log_level, message, unused_data);
	}
}

bool setupGtkBase(QLibrary &lib_gtk) {
	if (!LOAD_SYMBOL(lib_gtk, "gtk_init_check", gtk_init_check)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_settings_get_default", gtk_settings_get_default)) return false;

	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_show", gtk_widget_show)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_hide", gtk_widget_hide)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_get_window", gtk_widget_get_window)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_realize", gtk_widget_realize)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_hide_on_delete", gtk_widget_hide_on_delete)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_widget_destroy", gtk_widget_destroy)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_clipboard_get", gtk_clipboard_get)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_clipboard_store", gtk_clipboard_store)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_clipboard_wait_for_contents", gtk_clipboard_wait_for_contents)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_clipboard_wait_for_image", gtk_clipboard_wait_for_image)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_selection_data_targets_include_image", gtk_selection_data_targets_include_image)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_selection_data_free", gtk_selection_data_free)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_dialog_new", gtk_file_chooser_dialog_new)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_get_type", gtk_file_chooser_get_type)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_image_get_type", gtk_image_get_type)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_current_folder", gtk_file_chooser_set_current_folder)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_get_current_folder", gtk_file_chooser_get_current_folder)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_current_name", gtk_file_chooser_set_current_name)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_select_filename", gtk_file_chooser_select_filename)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_get_filenames", gtk_file_chooser_get_filenames)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_filter", gtk_file_chooser_set_filter)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_get_filter", gtk_file_chooser_get_filter)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_window_get_type", gtk_window_get_type)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_window_set_title", gtk_window_set_title)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_local_only", gtk_file_chooser_set_local_only)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_action", gtk_file_chooser_set_action)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_select_multiple", gtk_file_chooser_set_select_multiple)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_do_overwrite_confirmation", gtk_file_chooser_set_do_overwrite_confirmation)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_remove_filter", gtk_file_chooser_remove_filter)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_filter_set_name", gtk_file_filter_set_name)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_filter_add_pattern", gtk_file_filter_add_pattern)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_add_filter", gtk_file_chooser_add_filter)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_preview_widget", gtk_file_chooser_set_preview_widget)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_get_preview_filename", gtk_file_chooser_get_preview_filename)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_chooser_set_preview_widget_active", gtk_file_chooser_set_preview_widget_active)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_file_filter_new", gtk_file_filter_new)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_image_new", gtk_image_new)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_image_set_from_pixbuf", gtk_image_set_from_pixbuf)) return false;

	if (!LOAD_SYMBOL(lib_gtk, "gdk_window_set_modal_hint", gdk_window_set_modal_hint)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gdk_window_focus", gdk_window_focus)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_dialog_get_type", gtk_dialog_get_type)) return false;
	if (!LOAD_SYMBOL(lib_gtk, "gtk_dialog_run", gtk_dialog_run)) return false;

	if (!LOAD_SYMBOL(lib_gtk, "gdk_atom_intern", gdk_atom_intern)) return false;

	if (LOAD_SYMBOL(lib_gtk, "gdk_set_allowed_backends", gdk_set_allowed_backends)) {
		// We work only with X11 GDK backend.
		// Otherwise we get segfault in Ubuntu 17.04 in gtk_init_check() call.
		// See https://github.com/telegramdesktop/tdesktop/issues/3176
		// See https://github.com/telegramdesktop/tdesktop/issues/3162
		if(Platform::IsWayland() && !lib_gtk.fileName().contains("gtk-x11-2.0")) {
			DEBUG_LOG(("Limit allowed GDK backends to wayland"));
			gdk_set_allowed_backends("wayland");
		} else {
			DEBUG_LOG(("Limit allowed GDK backends to x11"));
			gdk_set_allowed_backends("x11");
		}
	}

	// gtk_init will reset the Xlib error handler, and that causes
	// Qt applications to quit on X errors. Therefore, we need to manually restore it.
	internal::XErrorHandlerRestorer handlerRestorer;
	handlerRestorer.save();

	DEBUG_LOG(("Library gtk functions loaded!"));
	gtkTriedToInit = true;
	if (!gtk_init_check(0, 0)) {
		gtk_init_check = nullptr;
		DEBUG_LOG(("Failed to gtk_init_check(0, 0)!"));
		return false;
	}
	DEBUG_LOG(("Checked gtk with gtk_init_check!"));

	handlerRestorer.restore();

	// Use our custom log handler.
	g_log_set_handler("Gtk", G_LOG_LEVEL_MESSAGE, gtkMessageHandler, nullptr);

	return true;
}

bool IconThemeShouldBeSet() {
	// change the icon theme only if it isn't already set by a platformtheme plugin
	// if QT_QPA_PLATFORMTHEME=(gtk2|gtk3), then force-apply the icon theme
	static const auto Result = ((QIcon::themeName() == qstr("hicolor") // QGenericUnixTheme
		&& QIcon::fallbackThemeName() == qstr("hicolor"))
		|| (QIcon::themeName() == qstr("Adwaita") // QGnomeTheme
		&& QIcon::fallbackThemeName() == qstr("gnome")))
		|| IsGtkIntegrationForced();

	return Result;
}

void SetIconTheme() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		if (IconThemeShouldBeSet()) {
			DEBUG_LOG(("Set GTK icon theme"));
			QIcon::setThemeName(gtkSetting("gtk-icon-theme-name"));
			QIcon::setFallbackThemeName(gtkSetting("gtk-fallback-icon-theme"));
			Platform::SetApplicationIcon(Window::CreateIcon());
			if (App::wnd()) {
				App::wnd()->setWindowIcon(Window::CreateIcon());
			}
			Core::App().domain().notifyUnreadBadgeChanged();
		}
	});
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
f_gtk_init_check gtk_init_check = nullptr;
f_gtk_settings_get_default gtk_settings_get_default = nullptr;
f_gtk_widget_show gtk_widget_show = nullptr;
f_gtk_widget_hide gtk_widget_hide = nullptr;
f_gtk_widget_get_window gtk_widget_get_window = nullptr;
f_gtk_widget_realize gtk_widget_realize = nullptr;
f_gtk_widget_hide_on_delete gtk_widget_hide_on_delete = nullptr;
f_gtk_widget_destroy gtk_widget_destroy = nullptr;
f_gtk_clipboard_get gtk_clipboard_get = nullptr;
f_gtk_clipboard_store gtk_clipboard_store = nullptr;
f_gtk_clipboard_wait_for_contents gtk_clipboard_wait_for_contents = nullptr;
f_gtk_clipboard_wait_for_image gtk_clipboard_wait_for_image = nullptr;
f_gtk_selection_data_targets_include_image gtk_selection_data_targets_include_image = nullptr;
f_gtk_selection_data_free gtk_selection_data_free = nullptr;
f_gtk_file_chooser_dialog_new gtk_file_chooser_dialog_new = nullptr;
f_gtk_file_chooser_get_type gtk_file_chooser_get_type = nullptr;
f_gtk_image_get_type gtk_image_get_type = nullptr;
f_gtk_file_chooser_set_current_folder gtk_file_chooser_set_current_folder = nullptr;
f_gtk_file_chooser_get_current_folder gtk_file_chooser_get_current_folder = nullptr;
f_gtk_file_chooser_set_current_name gtk_file_chooser_set_current_name = nullptr;
f_gtk_file_chooser_select_filename gtk_file_chooser_select_filename = nullptr;
f_gtk_file_chooser_get_filenames gtk_file_chooser_get_filenames = nullptr;
f_gtk_file_chooser_set_filter gtk_file_chooser_set_filter = nullptr;
f_gtk_file_chooser_get_filter gtk_file_chooser_get_filter = nullptr;
f_gtk_window_get_type gtk_window_get_type = nullptr;
f_gtk_window_set_title gtk_window_set_title = nullptr;
f_gtk_file_chooser_set_local_only gtk_file_chooser_set_local_only = nullptr;
f_gtk_file_chooser_set_action gtk_file_chooser_set_action = nullptr;
f_gtk_file_chooser_set_select_multiple gtk_file_chooser_set_select_multiple = nullptr;
f_gtk_file_chooser_set_do_overwrite_confirmation gtk_file_chooser_set_do_overwrite_confirmation = nullptr;
f_gtk_file_chooser_remove_filter gtk_file_chooser_remove_filter = nullptr;
f_gtk_file_filter_set_name gtk_file_filter_set_name = nullptr;
f_gtk_file_filter_add_pattern gtk_file_filter_add_pattern = nullptr;
f_gtk_file_chooser_add_filter gtk_file_chooser_add_filter = nullptr;
f_gtk_file_chooser_set_preview_widget gtk_file_chooser_set_preview_widget = nullptr;
f_gtk_file_chooser_get_preview_filename gtk_file_chooser_get_preview_filename = nullptr;
f_gtk_file_chooser_set_preview_widget_active gtk_file_chooser_set_preview_widget_active = nullptr;
f_gtk_file_filter_new gtk_file_filter_new = nullptr;
f_gtk_image_new gtk_image_new = nullptr;
f_gtk_image_set_from_pixbuf gtk_image_set_from_pixbuf = nullptr;
f_gtk_dialog_get_widget_for_response gtk_dialog_get_widget_for_response = nullptr;
f_gtk_button_set_label gtk_button_set_label = nullptr;
f_gtk_button_get_type gtk_button_get_type = nullptr;
f_gdk_set_allowed_backends gdk_set_allowed_backends = nullptr;
f_gdk_window_set_modal_hint gdk_window_set_modal_hint = nullptr;
f_gdk_window_focus gdk_window_focus = nullptr;
f_gtk_dialog_get_type gtk_dialog_get_type = nullptr;
f_gtk_dialog_run gtk_dialog_run = nullptr;
f_gdk_atom_intern gdk_atom_intern = nullptr;
f_gdk_pixbuf_new_from_file_at_size gdk_pixbuf_new_from_file_at_size = nullptr;
f_gdk_pixbuf_get_has_alpha gdk_pixbuf_get_has_alpha = nullptr;
f_gdk_pixbuf_get_pixels gdk_pixbuf_get_pixels = nullptr;
f_gdk_pixbuf_get_width gdk_pixbuf_get_width = nullptr;
f_gdk_pixbuf_get_height gdk_pixbuf_get_height = nullptr;
f_gdk_pixbuf_get_rowstride gdk_pixbuf_get_rowstride = nullptr;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

void start() {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (!UseGtkIntegration()) {
		return;
	}

	DEBUG_LOG(("Loading libraries"));

	bool gtkLoaded = false;
	QLibrary lib_gtk;
	lib_gtk.setLoadHints(QLibrary::DeepBindHint);

	if (loadLibrary(lib_gtk, "gtk-3", 0)) {
		gtkLoaded = setupGtkBase(lib_gtk);
	}
	if (!gtkLoaded && !gtkTriedToInit && loadLibrary(lib_gtk, "gtk-x11-2.0", 0)) {
		gtkLoaded = setupGtkBase(lib_gtk);
	}

	if (gtkLoaded) {
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_new_from_file_at_size", gdk_pixbuf_new_from_file_at_size);
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_get_has_alpha", gdk_pixbuf_get_has_alpha);
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_get_pixels", gdk_pixbuf_get_pixels);
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_get_width", gdk_pixbuf_get_width);
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_get_height", gdk_pixbuf_get_height);
		LOAD_SYMBOL(lib_gtk, "gdk_pixbuf_get_rowstride", gdk_pixbuf_get_rowstride);

		internal::GdkHelperLoad(lib_gtk);

		LOAD_SYMBOL(lib_gtk, "gtk_dialog_get_widget_for_response", gtk_dialog_get_widget_for_response);
		LOAD_SYMBOL(lib_gtk, "gtk_button_set_label", gtk_button_set_label);
		LOAD_SYMBOL(lib_gtk, "gtk_button_get_type", gtk_button_get_type);

		SetIconTheme();

		const auto settings = gtk_settings_get_default();
		g_signal_connect(settings, "notify::gtk-icon-theme-name", G_CALLBACK(SetIconTheme), nullptr);
	} else {
		LOG(("Could not load gtk-3 or gtk-x11-2.0!"));
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
}

} // namespace Libs
} // namespace Platform
