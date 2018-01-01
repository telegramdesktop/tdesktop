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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "platform/linux/linux_libs.h"

#include "platform/linux/linux_gdk_helper.h"
#include "platform/linux/linux_libnotify.h"
#include "platform/linux/linux_desktop_environment.h"

namespace Platform {
namespace Libs {
namespace {

bool loadLibrary(QLibrary &lib, const char *name, int version) {
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
}

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
bool setupGtkBase(QLibrary &lib_gtk) {
	if (!load(lib_gtk, "gtk_init_check", gtk_init_check)) return false;
	if (!load(lib_gtk, "gtk_menu_new", gtk_menu_new)) return false;
	if (!load(lib_gtk, "gtk_menu_get_type", gtk_menu_get_type)) return false;

	if (!load(lib_gtk, "gtk_menu_item_new_with_label", gtk_menu_item_new_with_label)) return false;
	if (!load(lib_gtk, "gtk_menu_item_set_label", gtk_menu_item_set_label)) return false;
	if (!load(lib_gtk, "gtk_menu_shell_append", gtk_menu_shell_append)) return false;
	if (!load(lib_gtk, "gtk_menu_shell_get_type", gtk_menu_shell_get_type)) return false;
	if (!load(lib_gtk, "gtk_widget_show", gtk_widget_show)) return false;
	if (!load(lib_gtk, "gtk_widget_hide", gtk_widget_hide)) return false;
	if (!load(lib_gtk, "gtk_widget_get_toplevel", gtk_widget_get_toplevel)) return false;
	if (!load(lib_gtk, "gtk_widget_get_visible", gtk_widget_get_visible)) return false;
	if (!load(lib_gtk, "gtk_widget_get_window", gtk_widget_get_window)) return false;
	if (!load(lib_gtk, "gtk_widget_set_sensitive", gtk_widget_set_sensitive)) return false;
	if (!load(lib_gtk, "gtk_widget_realize", gtk_widget_realize)) return false;
	if (!load(lib_gtk, "gtk_widget_hide_on_delete", gtk_widget_hide_on_delete)) return false;
	if (!load(lib_gtk, "gtk_widget_destroy", gtk_widget_destroy)) return false;
	if (!load(lib_gtk, "gtk_clipboard_get", gtk_clipboard_get)) return false;
	if (!load(lib_gtk, "gtk_clipboard_store", gtk_clipboard_store)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_dialog_new", gtk_file_chooser_dialog_new)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_get_type", gtk_file_chooser_get_type)) return false;
	if (!load(lib_gtk, "gtk_image_get_type", gtk_image_get_type)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_current_folder", gtk_file_chooser_set_current_folder)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_get_current_folder", gtk_file_chooser_get_current_folder)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_current_name", gtk_file_chooser_set_current_name)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_select_filename", gtk_file_chooser_select_filename)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_get_filenames", gtk_file_chooser_get_filenames)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_filter", gtk_file_chooser_set_filter)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_get_filter", gtk_file_chooser_get_filter)) return false;
	if (!load(lib_gtk, "gtk_window_get_type", gtk_window_get_type)) return false;
	if (!load(lib_gtk, "gtk_window_set_title", gtk_window_set_title)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_local_only", gtk_file_chooser_set_local_only)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_action", gtk_file_chooser_set_action)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_select_multiple", gtk_file_chooser_set_select_multiple)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_do_overwrite_confirmation", gtk_file_chooser_set_do_overwrite_confirmation)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_remove_filter", gtk_file_chooser_remove_filter)) return false;
	if (!load(lib_gtk, "gtk_file_filter_set_name", gtk_file_filter_set_name)) return false;
	if (!load(lib_gtk, "gtk_file_filter_add_pattern", gtk_file_filter_add_pattern)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_add_filter", gtk_file_chooser_add_filter)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_preview_widget", gtk_file_chooser_set_preview_widget)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_get_preview_filename", gtk_file_chooser_get_preview_filename)) return false;
	if (!load(lib_gtk, "gtk_file_chooser_set_preview_widget_active", gtk_file_chooser_set_preview_widget_active)) return false;
	if (!load(lib_gtk, "gtk_file_filter_new", gtk_file_filter_new)) return false;
	if (!load(lib_gtk, "gtk_image_new", gtk_image_new)) return false;
	if (!load(lib_gtk, "gtk_image_set_from_pixbuf", gtk_image_set_from_pixbuf)) return false;

	if (!load(lib_gtk, "gdk_window_set_modal_hint", gdk_window_set_modal_hint)) return false;
	if (!load(lib_gtk, "gdk_window_focus", gdk_window_focus)) return false;
	if (!load(lib_gtk, "gtk_dialog_get_type", gtk_dialog_get_type)) return false;
	if (!load(lib_gtk, "gtk_dialog_run", gtk_dialog_run)) return false;

	if (!load(lib_gtk, "g_type_check_instance_cast", g_type_check_instance_cast)) return false;
	if (!load(lib_gtk, "g_type_check_instance_is_a", g_type_check_instance_is_a)) return false;
	if (!load(lib_gtk, "g_signal_connect_data", g_signal_connect_data)) return false;
	if (!load(lib_gtk, "g_signal_handler_disconnect", g_signal_handler_disconnect)) return false;

	if (!load(lib_gtk, "g_object_ref_sink", g_object_ref_sink)) return false;
	if (!load(lib_gtk, "g_object_unref", g_object_unref)) return false;
	if (!load(lib_gtk, "g_free", g_free)) return false;
	if (!load(lib_gtk, "g_list_foreach", g_list_foreach)) return false;
	if (!load(lib_gtk, "g_list_free", g_list_free)) return false;
	if (!load(lib_gtk, "g_list_free_full", g_list_free_full)) return false;

	if (!load(lib_gtk, "g_error_free", g_error_free)) return false;
	if (!load(lib_gtk, "g_slist_free", g_slist_free)) return false;

	DEBUG_LOG(("Library gtk functions loaded!"));

	if (load(lib_gtk, "gdk_set_allowed_backends", gdk_set_allowed_backends)) {
		// We work only with X11 GDK backend.
		// Otherwise we get segfault in Ubuntu 17.04 in gtk_init_check() call.
		// See https://github.com/telegramdesktop/tdesktop/issues/3176
		// See https://github.com/telegramdesktop/tdesktop/issues/3162
		DEBUG_LOG(("Limit allowed GDK backends to x11"));
		gdk_set_allowed_backends("x11");
	}

	DEBUG_LOG(("Library gtk functions loaded!"));
	if (!gtk_init_check(0, 0)) {
		gtk_init_check = nullptr;
		DEBUG_LOG(("Failed to gtk_init_check(0, 0)!"));
		return false;
	}

	DEBUG_LOG(("Checked gtk with gtk_init_check!"));
	return true;
}

bool setupAppIndicator(QLibrary &lib_indicator) {
	if (!load(lib_indicator, "app_indicator_new", app_indicator_new)) return false;
	if (!load(lib_indicator, "app_indicator_set_status", app_indicator_set_status)) return false;
	if (!load(lib_indicator, "app_indicator_set_menu", app_indicator_set_menu)) return false;
	if (!load(lib_indicator, "app_indicator_set_icon_full", app_indicator_set_icon_full)) return false;

	DEBUG_LOG(("Library appindicator functions loaded!"));
	return true;
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
f_gtk_init_check gtk_init_check = nullptr;
f_gtk_menu_new gtk_menu_new = nullptr;
f_gtk_menu_get_type gtk_menu_get_type = nullptr;
f_gtk_menu_item_new_with_label gtk_menu_item_new_with_label = nullptr;
f_gtk_menu_item_set_label gtk_menu_item_set_label = nullptr;
f_gtk_menu_shell_append gtk_menu_shell_append = nullptr;
f_gtk_menu_shell_get_type gtk_menu_shell_get_type = nullptr;
f_gtk_widget_show gtk_widget_show = nullptr;
f_gtk_widget_hide gtk_widget_hide = nullptr;
f_gtk_widget_get_toplevel gtk_widget_get_toplevel = nullptr;
f_gtk_widget_get_visible gtk_widget_get_visible = nullptr;
f_gtk_widget_get_window gtk_widget_get_window = nullptr;
f_gtk_widget_set_sensitive gtk_widget_set_sensitive = nullptr;
f_gtk_widget_realize gtk_widget_realize = nullptr;
f_gtk_widget_hide_on_delete gtk_widget_hide_on_delete = nullptr;
f_gtk_widget_destroy gtk_widget_destroy = nullptr;
f_gtk_clipboard_get gtk_clipboard_get = nullptr;
f_gtk_clipboard_store gtk_clipboard_store = nullptr;
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
f_g_type_check_instance_cast g_type_check_instance_cast = nullptr;
f_g_type_check_instance_is_a g_type_check_instance_is_a = nullptr;
f_g_signal_connect_data g_signal_connect_data = nullptr;
f_g_signal_handler_disconnect g_signal_handler_disconnect = nullptr;
f_app_indicator_new app_indicator_new = nullptr;
f_app_indicator_set_status app_indicator_set_status = nullptr;
f_app_indicator_set_menu app_indicator_set_menu = nullptr;
f_app_indicator_set_icon_full app_indicator_set_icon_full = nullptr;
f_gdk_init_check gdk_init_check = nullptr;
f_gdk_pixbuf_new_from_data gdk_pixbuf_new_from_data = nullptr;
f_gdk_pixbuf_new_from_file gdk_pixbuf_new_from_file = nullptr;
f_gdk_pixbuf_new_from_file_at_size gdk_pixbuf_new_from_file_at_size = nullptr;
f_gtk_status_icon_new_from_pixbuf gtk_status_icon_new_from_pixbuf = nullptr;
f_gtk_status_icon_set_from_pixbuf gtk_status_icon_set_from_pixbuf = nullptr;
f_gtk_status_icon_new_from_file gtk_status_icon_new_from_file = nullptr;
f_gtk_status_icon_set_from_file gtk_status_icon_set_from_file = nullptr;
f_gtk_status_icon_set_title gtk_status_icon_set_title = nullptr;
f_gtk_status_icon_set_tooltip_text gtk_status_icon_set_tooltip_text = nullptr;
f_gtk_status_icon_set_visible gtk_status_icon_set_visible = nullptr;
f_gtk_status_icon_is_embedded gtk_status_icon_is_embedded = nullptr;
f_gtk_status_icon_get_geometry gtk_status_icon_get_geometry = nullptr;
f_gtk_status_icon_position_menu gtk_status_icon_position_menu = nullptr;
f_gtk_menu_popup gtk_menu_popup = nullptr;
f_gtk_get_current_event_time gtk_get_current_event_time = nullptr;
f_g_object_ref_sink g_object_ref_sink = nullptr;
f_g_object_unref g_object_unref = nullptr;
f_g_idle_add g_idle_add = nullptr;
f_g_free g_free = nullptr;
f_g_list_foreach g_list_foreach = nullptr;
f_g_list_free g_list_free = nullptr;
f_g_list_free_full g_list_free_full = nullptr;
f_g_error_free g_error_free = nullptr;
f_g_slist_free g_slist_free = nullptr;
#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
f_unity_launcher_entry_set_count unity_launcher_entry_set_count = nullptr;
f_unity_launcher_entry_set_count_visible unity_launcher_entry_set_count_visible = nullptr;
f_unity_launcher_entry_get_for_desktop_id unity_launcher_entry_get_for_desktop_id = nullptr;
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

void start() {
	DEBUG_LOG(("Loading libraries"));
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION

	bool gtkLoaded = false;
	bool indicatorLoaded = false;
	QLibrary lib_gtk, lib_indicator;
	if (loadLibrary(lib_indicator, "appindicator3", 1)) {
		if (loadLibrary(lib_gtk, "gtk-3", 0)) {
			gtkLoaded = setupGtkBase(lib_gtk);
			indicatorLoaded = setupAppIndicator(lib_indicator);
		}
	}
	if (!gtkLoaded || !indicatorLoaded) {
		if (loadLibrary(lib_indicator, "appindicator", 1)) {
			if (loadLibrary(lib_gtk, "gtk-x11-2.0", 0)) {
				gtkLoaded = indicatorLoaded = false;
				gtkLoaded = setupGtkBase(lib_gtk);
				indicatorLoaded = setupAppIndicator(lib_indicator);
			}
		}
	}

	// If no appindicator, try at least load gtk.
	if (!gtkLoaded && !indicatorLoaded) {
		if (loadLibrary(lib_gtk, "gtk-3", 0)) {
			gtkLoaded = setupGtkBase(lib_gtk);
		}
		if (!gtkLoaded && loadLibrary(lib_gtk, "gtk-x11-2.0", 0)) {
			gtkLoaded = setupGtkBase(lib_gtk);
		}
	}

	if (gtkLoaded) {
		load(lib_gtk, "gdk_init_check", gdk_init_check);
		load(lib_gtk, "gdk_pixbuf_new_from_data", gdk_pixbuf_new_from_data);
		load(lib_gtk, "gdk_pixbuf_new_from_file", gdk_pixbuf_new_from_file);
		load(lib_gtk, "gdk_pixbuf_new_from_file_at_size", gdk_pixbuf_new_from_file_at_size);
		load(lib_gtk, "gtk_status_icon_new_from_pixbuf", gtk_status_icon_new_from_pixbuf);
		load(lib_gtk, "gtk_status_icon_set_from_pixbuf", gtk_status_icon_set_from_pixbuf);
		load(lib_gtk, "gtk_status_icon_new_from_file", gtk_status_icon_new_from_file);
		load(lib_gtk, "gtk_status_icon_set_from_file", gtk_status_icon_set_from_file);
		load(lib_gtk, "gtk_status_icon_set_title", gtk_status_icon_set_title);
		load(lib_gtk, "gtk_status_icon_set_tooltip_text", gtk_status_icon_set_tooltip_text);
		load(lib_gtk, "gtk_status_icon_set_visible", gtk_status_icon_set_visible);
		load(lib_gtk, "gtk_status_icon_is_embedded", gtk_status_icon_is_embedded);
		load(lib_gtk, "gtk_status_icon_get_geometry", gtk_status_icon_get_geometry);
		load(lib_gtk, "gtk_status_icon_position_menu", gtk_status_icon_position_menu);
		load(lib_gtk, "gtk_menu_popup", gtk_menu_popup);
		load(lib_gtk, "gtk_get_current_event_time", gtk_get_current_event_time);
		load(lib_gtk, "g_idle_add", g_idle_add);

		internal::GdkHelperLoad(lib_gtk);

		load(lib_gtk, "gtk_dialog_get_widget_for_response", gtk_dialog_get_widget_for_response);
		load(lib_gtk, "gtk_button_set_label", gtk_button_set_label);
		load(lib_gtk, "gtk_button_get_type", gtk_button_get_type);
	} else {
		LOG(("Could not load gtk-x11-2.0!"));
	}

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
	if (DesktopEnvironment::TryUnityCounter()) {
		QLibrary lib_unity(qstr("unity"), 9, 0);
		loadLibrary(lib_unity, "unity", 9);

		load(lib_unity, "unity_launcher_entry_get_for_desktop_id", unity_launcher_entry_get_for_desktop_id);
		load(lib_unity, "unity_launcher_entry_set_count", unity_launcher_entry_set_count);
		load(lib_unity, "unity_launcher_entry_set_count_visible", unity_launcher_entry_set_count_visible);
	}
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION

	if (gtkLoaded) {
		startLibNotify();
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
}

} // namespace Libs
} // namespace Platform
