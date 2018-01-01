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
#pragma once

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
extern "C" {
#undef signals
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define signals public
} // extern "C"

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
#include <unity/unity/unity.h>
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

namespace Platform {
namespace Libs {

void start();

template <typename Function>
bool load(QLibrary &lib, const char *name, Function &func) {
	func = nullptr;
	if (!lib.isLoaded()) {
		return false;
	}

	func = reinterpret_cast<Function>(lib.resolve(name));
	if (func) {
		return true;
	}
	LOG(("Error: failed to load '%1' function!").arg(name));
	return false;
}

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
typedef gboolean (*f_gtk_init_check)(int *argc, char ***argv);
extern f_gtk_init_check gtk_init_check;

typedef GtkWidget* (*f_gtk_menu_new)(void);
extern f_gtk_menu_new gtk_menu_new;

typedef GType (*f_gtk_menu_get_type)(void) G_GNUC_CONST;
extern f_gtk_menu_get_type gtk_menu_get_type;

typedef GtkWidget* (*f_gtk_menu_item_new_with_label)(const gchar *label);
extern f_gtk_menu_item_new_with_label gtk_menu_item_new_with_label;

typedef void (*f_gtk_menu_item_set_label)(GtkMenuItem *menu_item, const gchar *label);
extern f_gtk_menu_item_set_label gtk_menu_item_set_label;

typedef void (*f_gtk_menu_shell_append)(GtkMenuShell *menu_shell, GtkWidget *child);
extern f_gtk_menu_shell_append gtk_menu_shell_append;

typedef GType (*f_gtk_menu_shell_get_type)(void) G_GNUC_CONST;
extern f_gtk_menu_shell_get_type gtk_menu_shell_get_type;

typedef void (*f_gtk_widget_show)(GtkWidget *widget);
extern f_gtk_widget_show gtk_widget_show;

typedef void (*f_gtk_widget_hide)(GtkWidget *widget);
extern f_gtk_widget_hide gtk_widget_hide;

typedef GtkWidget* (*f_gtk_widget_get_toplevel)(GtkWidget *widget);
extern f_gtk_widget_get_toplevel gtk_widget_get_toplevel;

typedef gboolean (*f_gtk_widget_get_visible)(GtkWidget *widget);
extern f_gtk_widget_get_visible gtk_widget_get_visible;

typedef GdkWindow* (*f_gtk_widget_get_window)(GtkWidget *widget);
extern f_gtk_widget_get_window gtk_widget_get_window;

typedef void (*f_gtk_widget_set_sensitive)(GtkWidget *widget, gboolean sensitive);
extern f_gtk_widget_set_sensitive gtk_widget_set_sensitive;

typedef void (*f_gtk_widget_realize)(GtkWidget *widget);
extern f_gtk_widget_realize gtk_widget_realize;

typedef gboolean (*f_gtk_widget_hide_on_delete)(GtkWidget *widget);
extern f_gtk_widget_hide_on_delete gtk_widget_hide_on_delete;

typedef void (*f_gtk_widget_destroy)(GtkWidget *widget);
extern f_gtk_widget_destroy gtk_widget_destroy;

typedef GtkClipboard* (*f_gtk_clipboard_get)(GdkAtom selection);
extern f_gtk_clipboard_get gtk_clipboard_get;

typedef void (*f_gtk_clipboard_store)(GtkClipboard *clipboard);
extern f_gtk_clipboard_store gtk_clipboard_store;

typedef GtkWidget* (*f_gtk_file_chooser_dialog_new)(const gchar *title, GtkWindow *parent, GtkFileChooserAction action, const gchar *first_button_text, ...) G_GNUC_NULL_TERMINATED;
extern f_gtk_file_chooser_dialog_new gtk_file_chooser_dialog_new;

typedef gboolean (*f_gtk_file_chooser_set_current_folder)(GtkFileChooser *chooser, const gchar *filename);
extern f_gtk_file_chooser_set_current_folder gtk_file_chooser_set_current_folder;

typedef gchar* (*f_gtk_file_chooser_get_current_folder)(GtkFileChooser *chooser);
extern f_gtk_file_chooser_get_current_folder gtk_file_chooser_get_current_folder;

typedef void (*f_gtk_file_chooser_set_current_name)(GtkFileChooser *chooser, const gchar *name);
extern f_gtk_file_chooser_set_current_name gtk_file_chooser_set_current_name;

typedef gboolean (*f_gtk_file_chooser_select_filename)(GtkFileChooser *chooser, const gchar *filename);
extern f_gtk_file_chooser_select_filename gtk_file_chooser_select_filename;

typedef GSList* (*f_gtk_file_chooser_get_filenames)(GtkFileChooser *chooser);
extern f_gtk_file_chooser_get_filenames gtk_file_chooser_get_filenames;

typedef void (*f_gtk_file_chooser_set_filter)(GtkFileChooser *chooser, GtkFileFilter *filter);
extern f_gtk_file_chooser_set_filter gtk_file_chooser_set_filter;

typedef GtkFileFilter* (*f_gtk_file_chooser_get_filter)(GtkFileChooser *chooser);
extern f_gtk_file_chooser_get_filter gtk_file_chooser_get_filter;

typedef void (*f_gtk_window_set_title)(GtkWindow *window, const gchar *title);
extern f_gtk_window_set_title gtk_window_set_title;

typedef void (*f_gtk_file_chooser_set_local_only)(GtkFileChooser *chooser, gboolean local_only);
extern f_gtk_file_chooser_set_local_only gtk_file_chooser_set_local_only;

typedef void (*f_gtk_file_chooser_set_action)(GtkFileChooser *chooser, GtkFileChooserAction action);
extern f_gtk_file_chooser_set_action gtk_file_chooser_set_action;

typedef void (*f_gtk_file_chooser_set_select_multiple)(GtkFileChooser *chooser, gboolean select_multiple);
extern f_gtk_file_chooser_set_select_multiple gtk_file_chooser_set_select_multiple;

typedef void (*f_gtk_file_chooser_set_do_overwrite_confirmation)(GtkFileChooser *chooser, gboolean do_overwrite_confirmation);
extern f_gtk_file_chooser_set_do_overwrite_confirmation gtk_file_chooser_set_do_overwrite_confirmation;

typedef GtkWidget* (*f_gtk_dialog_get_widget_for_response)(GtkDialog *dialog, gint response_id);
extern f_gtk_dialog_get_widget_for_response gtk_dialog_get_widget_for_response;

typedef void (*f_gtk_button_set_label)(GtkButton *button, const gchar *label);
extern f_gtk_button_set_label gtk_button_set_label;

typedef void (*f_gtk_file_chooser_remove_filter)(GtkFileChooser *chooser, GtkFileFilter *filter);
extern f_gtk_file_chooser_remove_filter gtk_file_chooser_remove_filter;

typedef void (*f_gtk_file_filter_set_name)(GtkFileFilter *filter, const gchar *name);
extern f_gtk_file_filter_set_name gtk_file_filter_set_name;

typedef void (*f_gtk_file_filter_add_pattern)(GtkFileFilter *filter, const gchar *pattern);
extern f_gtk_file_filter_add_pattern gtk_file_filter_add_pattern;

typedef void (*f_gtk_file_chooser_add_filter)(GtkFileChooser *chooser, GtkFileFilter *filter);
extern f_gtk_file_chooser_add_filter gtk_file_chooser_add_filter;

typedef void (*f_gtk_file_chooser_set_preview_widget)(GtkFileChooser *chooser, GtkWidget *preview_widget);
extern f_gtk_file_chooser_set_preview_widget gtk_file_chooser_set_preview_widget;

typedef gchar* (*f_gtk_file_chooser_get_preview_filename)(GtkFileChooser *chooser);
extern f_gtk_file_chooser_get_preview_filename gtk_file_chooser_get_preview_filename;

typedef void (*f_gtk_file_chooser_set_preview_widget_active)(GtkFileChooser *chooser, gboolean active);
extern f_gtk_file_chooser_set_preview_widget_active gtk_file_chooser_set_preview_widget_active;

typedef GtkFileFilter* (*f_gtk_file_filter_new)(void);
extern f_gtk_file_filter_new gtk_file_filter_new;

typedef GtkWidget* (*f_gtk_image_new)(void);
extern f_gtk_image_new gtk_image_new;

typedef void (*f_gtk_image_set_from_pixbuf)(GtkImage *image, GdkPixbuf *pixbuf);
extern f_gtk_image_set_from_pixbuf gtk_image_set_from_pixbuf;

typedef void (*f_gdk_set_allowed_backends)(const gchar *backends);
extern f_gdk_set_allowed_backends gdk_set_allowed_backends;

typedef void (*f_gdk_window_set_modal_hint)(GdkWindow *window, gboolean modal);
extern f_gdk_window_set_modal_hint gdk_window_set_modal_hint;

typedef void (*f_gdk_window_focus)(GdkWindow *window, guint32 timestamp);
extern f_gdk_window_focus gdk_window_focus;

typedef GTypeInstance* (*f_g_type_check_instance_cast)(GTypeInstance *instance, GType iface_type);
extern f_g_type_check_instance_cast g_type_check_instance_cast;

template <typename Result, typename Object>
inline Result *g_type_cic_helper(Object *instance, GType iface_type) {
	return reinterpret_cast<Result*>(g_type_check_instance_cast(reinterpret_cast<GTypeInstance*>(instance), iface_type));
}

template <typename Object>
inline GtkMenu *gtk_menu_cast(Object *obj) {
	return g_type_cic_helper<GtkMenu, Object>(obj, gtk_menu_get_type());
}

template <typename Object>
inline GtkMenuShell *gtk_menu_shell_cast(Object *obj) {
	return g_type_cic_helper<GtkMenuShell, Object>(obj, gtk_menu_get_type());
}

typedef GType (*f_gtk_dialog_get_type)(void) G_GNUC_CONST;
extern f_gtk_dialog_get_type gtk_dialog_get_type;

template <typename Object>
inline GtkDialog *gtk_dialog_cast(Object *obj) {
	return g_type_cic_helper<GtkDialog, Object>(obj, gtk_dialog_get_type());
}

template <typename Object>
inline GObject *g_object_cast(Object *obj) {
	return g_type_cic_helper<GObject, Object>(obj, G_TYPE_OBJECT);
}

typedef GType (*f_gtk_file_chooser_get_type)(void) G_GNUC_CONST;
extern f_gtk_file_chooser_get_type gtk_file_chooser_get_type;

template <typename Object>
inline GtkFileChooser *gtk_file_chooser_cast(Object *obj) {
	return g_type_cic_helper<GtkFileChooser, Object>(obj, gtk_file_chooser_get_type());
}

typedef GType (*f_gtk_image_get_type)(void) G_GNUC_CONST;
extern f_gtk_image_get_type gtk_image_get_type;

template <typename Object>
inline GtkImage *gtk_image_cast(Object *obj) {
	return g_type_cic_helper<GtkImage, Object>(obj, gtk_image_get_type());
}

typedef GType (*f_gtk_button_get_type)(void) G_GNUC_CONST;
extern f_gtk_button_get_type gtk_button_get_type;

template <typename Object>
inline GtkButton *gtk_button_cast(Object *obj) {
	return g_type_cic_helper<GtkButton, Object>(obj, gtk_button_get_type());
}

typedef GType (*f_gtk_window_get_type)(void) G_GNUC_CONST;
extern f_gtk_window_get_type gtk_window_get_type;

template <typename Object>
inline GtkWindow *gtk_window_cast(Object *obj) {
	return g_type_cic_helper<GtkWindow, Object>(obj, gtk_window_get_type());
}

typedef gboolean (*f_g_type_check_instance_is_a)(GTypeInstance *instance, GType iface_type);
extern f_g_type_check_instance_is_a g_type_check_instance_is_a;

template <typename Object>
inline bool g_type_cit_helper(Object *instance, GType iface_type) {
	if (!instance) return false;

	auto ginstance = reinterpret_cast<GTypeInstance*>(instance);
	if (ginstance->g_class && ginstance->g_class->g_type == iface_type) {
		return true;
	}
    return g_type_check_instance_is_a(ginstance, iface_type);
}

typedef gint (*f_gtk_dialog_run)(GtkDialog *dialog);
extern f_gtk_dialog_run gtk_dialog_run;

typedef gulong (*f_g_signal_connect_data)(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data, GConnectFlags connect_flags);
extern f_g_signal_connect_data g_signal_connect_data;

inline gulong g_signal_connect_helper(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data = nullptr) {
	return g_signal_connect_data(instance, detailed_signal, c_handler, data, destroy_data, (GConnectFlags)0);
}

inline gulong g_signal_connect_swapped_helper(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data = nullptr) {
	return g_signal_connect_data(instance, detailed_signal, c_handler, data, destroy_data, G_CONNECT_SWAPPED);
}

typedef void (*f_g_signal_handler_disconnect)(gpointer instance, gulong handler_id);
extern f_g_signal_handler_disconnect g_signal_handler_disconnect;

typedef AppIndicator* (*f_app_indicator_new)(const gchar *id, const gchar *icon_name, AppIndicatorCategory category);
extern f_app_indicator_new app_indicator_new;

typedef void (*f_app_indicator_set_status)(AppIndicator *self, AppIndicatorStatus status);
extern f_app_indicator_set_status app_indicator_set_status;

typedef void (*f_app_indicator_set_menu)(AppIndicator *self, GtkMenu *menu);
extern f_app_indicator_set_menu app_indicator_set_menu;

typedef void (*f_app_indicator_set_icon_full)(AppIndicator *self, const gchar *icon_name, const gchar *icon_desc);
extern f_app_indicator_set_icon_full app_indicator_set_icon_full;

typedef gboolean (*f_gdk_init_check)(gint *argc, gchar ***argv);
extern f_gdk_init_check gdk_init_check;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_data)(const guchar *data, GdkColorspace colorspace, gboolean has_alpha, int bits_per_sample, int width, int height, int rowstride, GdkPixbufDestroyNotify destroy_fn, gpointer destroy_fn_data);
extern f_gdk_pixbuf_new_from_data gdk_pixbuf_new_from_data;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_file)(const gchar *filename, GError **error);
extern f_gdk_pixbuf_new_from_file gdk_pixbuf_new_from_file;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_file_at_size)(const gchar *filename, int width, int height, GError **error);
extern f_gdk_pixbuf_new_from_file_at_size gdk_pixbuf_new_from_file_at_size;

typedef GtkStatusIcon* (*f_gtk_status_icon_new_from_pixbuf)(GdkPixbuf *pixbuf);
extern f_gtk_status_icon_new_from_pixbuf gtk_status_icon_new_from_pixbuf;

typedef void (*f_gtk_status_icon_set_from_pixbuf)(GtkStatusIcon *status_icon, GdkPixbuf *pixbuf);
extern f_gtk_status_icon_set_from_pixbuf gtk_status_icon_set_from_pixbuf;

typedef GtkStatusIcon* (*f_gtk_status_icon_new_from_file)(const gchar *filename);
extern f_gtk_status_icon_new_from_file gtk_status_icon_new_from_file;

typedef void (*f_gtk_status_icon_set_from_file)(GtkStatusIcon *status_icon, const gchar *filename);
extern f_gtk_status_icon_set_from_file gtk_status_icon_set_from_file;

typedef void (*f_gtk_status_icon_set_title)(GtkStatusIcon *status_icon, const gchar *title);
extern f_gtk_status_icon_set_title gtk_status_icon_set_title;

typedef void (*f_gtk_status_icon_set_tooltip_text)(GtkStatusIcon *status_icon, const gchar *title);
extern f_gtk_status_icon_set_tooltip_text gtk_status_icon_set_tooltip_text;

typedef void (*f_gtk_status_icon_set_visible)(GtkStatusIcon *status_icon, gboolean visible);
extern f_gtk_status_icon_set_visible gtk_status_icon_set_visible;

typedef gboolean (*f_gtk_status_icon_is_embedded)(GtkStatusIcon *status_icon);
extern f_gtk_status_icon_is_embedded gtk_status_icon_is_embedded;

typedef gboolean (*f_gtk_status_icon_get_geometry)(GtkStatusIcon *status_icon, GdkScreen **screen, GdkRectangle *area, GtkOrientation *orientation);
extern f_gtk_status_icon_get_geometry gtk_status_icon_get_geometry;

typedef void (*f_gtk_status_icon_position_menu)(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data);
extern f_gtk_status_icon_position_menu gtk_status_icon_position_menu;

typedef void (*f_gtk_menu_popup)(GtkMenu *menu, GtkWidget *parent_menu_shell, GtkWidget *parent_menu_item, GtkMenuPositionFunc func, gpointer data, guint button, guint32 activate_time);
extern f_gtk_menu_popup gtk_menu_popup;

typedef guint32 (*f_gtk_get_current_event_time)(void);
extern f_gtk_get_current_event_time gtk_get_current_event_time;

typedef gpointer (*f_g_object_ref_sink)(gpointer object);
extern f_g_object_ref_sink g_object_ref_sink;

typedef void (*f_g_object_unref)(gpointer object);
extern f_g_object_unref g_object_unref;

typedef guint (*f_g_idle_add)(GSourceFunc function, gpointer data);
extern f_g_idle_add g_idle_add;

typedef void (*f_g_free)(gpointer mem);
extern f_g_free g_free;

typedef void (*f_g_list_foreach)(GList *list, GFunc func, gpointer user_data);
extern f_g_list_foreach g_list_foreach;

typedef void (*f_g_list_free)(GList *list);
extern f_g_list_free g_list_free;

typedef void (*f_g_list_free_full)(GList *list, GDestroyNotify free_func);
extern f_g_list_free_full g_list_free_full;

typedef void (*f_g_error_free)(GError *error);
extern f_g_error_free g_error_free;

typedef void (*f_g_slist_free)(GSList *list);
extern f_g_slist_free g_slist_free;

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
typedef void (*f_unity_launcher_entry_set_count)(UnityLauncherEntry* self, gint64 value);
extern f_unity_launcher_entry_set_count unity_launcher_entry_set_count;

typedef void (*f_unity_launcher_entry_set_count_visible)(UnityLauncherEntry* self, gboolean value);
extern f_unity_launcher_entry_set_count_visible unity_launcher_entry_set_count_visible;

typedef UnityLauncherEntry* (*f_unity_launcher_entry_get_for_desktop_id)(const gchar* desktop_id);
extern f_unity_launcher_entry_get_for_desktop_id unity_launcher_entry_get_for_desktop_id;
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace Libs
} // namespace Platform
