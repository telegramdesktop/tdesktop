/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QLibrary>

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION

extern "C" {
#undef signals
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define signals public
} // extern "C"

// present starting with gtk 3.0, we can build with gtk2 headers
typedef struct _GtkAppChooser GtkAppChooser;

#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

#if defined DESKTOP_APP_USE_PACKAGED && !defined DESKTOP_APP_USE_PACKAGED_LAZY
#define LOAD_SYMBOL(lib, name, func) (func = ::func)
#else // DESKTOP_APP_USE_PACKAGED && !DESKTOP_APP_USE_PACKAGED_LAZY
#define LOAD_SYMBOL Platform::Libs::load
#endif // !DESKTOP_APP_USE_PACKAGED || DESKTOP_APP_USE_PACKAGED_LAZY

namespace Platform {
namespace Libs {

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
bool GtkLoaded();
::GtkClipboard *GtkClipboard();
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

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

typedef const gchar* (*f_gtk_check_version)(guint required_major, guint required_minor, guint required_micro);
extern f_gtk_check_version gtk_check_version;

typedef GtkSettings* (*f_gtk_settings_get_default)(void);
extern f_gtk_settings_get_default gtk_settings_get_default;

typedef void (*f_gtk_widget_show)(GtkWidget *widget);
extern f_gtk_widget_show gtk_widget_show;

typedef void (*f_gtk_widget_hide)(GtkWidget *widget);
extern f_gtk_widget_hide gtk_widget_hide;

typedef GdkWindow* (*f_gtk_widget_get_window)(GtkWidget *widget);
extern f_gtk_widget_get_window gtk_widget_get_window;

typedef void (*f_gtk_widget_realize)(GtkWidget *widget);
extern f_gtk_widget_realize gtk_widget_realize;

typedef gboolean (*f_gtk_widget_hide_on_delete)(GtkWidget *widget);
extern f_gtk_widget_hide_on_delete gtk_widget_hide_on_delete;

typedef void (*f_gtk_widget_destroy)(GtkWidget *widget);
extern f_gtk_widget_destroy gtk_widget_destroy;

typedef ::GtkClipboard* (*f_gtk_clipboard_get)(GdkAtom selection);
extern f_gtk_clipboard_get gtk_clipboard_get;

typedef void (*f_gtk_clipboard_store)(::GtkClipboard *clipboard);
extern f_gtk_clipboard_store gtk_clipboard_store;

typedef void (*f_gtk_clipboard_set_image)(::GtkClipboard *clipboard, GdkPixbuf *pixbuf);
extern f_gtk_clipboard_set_image gtk_clipboard_set_image;

typedef GtkSelectionData* (*f_gtk_clipboard_wait_for_contents)(::GtkClipboard *clipboard, GdkAtom target);
extern f_gtk_clipboard_wait_for_contents gtk_clipboard_wait_for_contents;

typedef GdkPixbuf* (*f_gtk_clipboard_wait_for_image)(::GtkClipboard *clipboard);
extern f_gtk_clipboard_wait_for_image gtk_clipboard_wait_for_image;

typedef gboolean (*f_gtk_selection_data_targets_include_image)(const GtkSelectionData *selection_data, gboolean writable);
extern f_gtk_selection_data_targets_include_image gtk_selection_data_targets_include_image;

typedef void (*f_gtk_selection_data_free)(GtkSelectionData *data);
extern f_gtk_selection_data_free gtk_selection_data_free;

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

typedef GtkWidget* (*f_gtk_app_chooser_dialog_new)(GtkWindow *parent, GtkDialogFlags flags, GFile *file);
extern f_gtk_app_chooser_dialog_new gtk_app_chooser_dialog_new;

typedef GAppInfo* (*f_gtk_app_chooser_get_app_info)(GtkAppChooser *self);
extern f_gtk_app_chooser_get_app_info gtk_app_chooser_get_app_info;

typedef void (*f_gdk_set_allowed_backends)(const gchar *backends);
extern f_gdk_set_allowed_backends gdk_set_allowed_backends;

typedef void (*f_gdk_window_set_modal_hint)(GdkWindow *window, gboolean modal);
extern f_gdk_window_set_modal_hint gdk_window_set_modal_hint;

typedef void (*f_gdk_window_focus)(GdkWindow *window, guint32 timestamp);
extern f_gdk_window_focus gdk_window_focus;

template <typename Result, typename Object>
inline Result *g_type_cic_helper(Object *instance, GType iface_type) {
	return reinterpret_cast<Result*>(g_type_check_instance_cast(reinterpret_cast<GTypeInstance*>(instance), iface_type));
}

typedef GType (*f_gtk_dialog_get_type)(void) G_GNUC_CONST;
extern f_gtk_dialog_get_type gtk_dialog_get_type;

template <typename Object>
inline GtkDialog *gtk_dialog_cast(Object *obj) {
	return g_type_cic_helper<GtkDialog, Object>(obj, gtk_dialog_get_type());
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

typedef GType (*f_gtk_widget_get_type)(void) G_GNUC_CONST;
extern f_gtk_widget_get_type gtk_widget_get_type;

template <typename Object>
inline GtkWidget *gtk_widget_cast(Object *obj) {
	return g_type_cic_helper<GtkWidget, Object>(obj, gtk_widget_get_type());
}

typedef GType (*f_gtk_app_chooser_get_type)(void) G_GNUC_CONST;
extern f_gtk_app_chooser_get_type gtk_app_chooser_get_type;

template <typename Object>
inline GtkAppChooser *gtk_app_chooser_cast(Object *obj) {
	return g_type_cic_helper<GtkAppChooser, Object>(obj, gtk_app_chooser_get_type());
}

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

typedef GdkAtom (*f_gdk_atom_intern)(const gchar *atom_name, gboolean only_if_exists);
extern f_gdk_atom_intern gdk_atom_intern;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_data)(const guchar *data, GdkColorspace colorspace, gboolean has_alpha, int bits_per_sample, int width, int height, int rowstride, GdkPixbufDestroyNotify destroy_fn, gpointer destroy_fn_data);
extern f_gdk_pixbuf_new_from_data gdk_pixbuf_new_from_data;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_file_at_size)(const gchar *filename, int width, int height, GError **error);
extern f_gdk_pixbuf_new_from_file_at_size gdk_pixbuf_new_from_file_at_size;

typedef gboolean (*f_gdk_pixbuf_get_has_alpha)(const GdkPixbuf *pixbuf);
extern f_gdk_pixbuf_get_has_alpha gdk_pixbuf_get_has_alpha;

typedef guchar* (*f_gdk_pixbuf_get_pixels)(const GdkPixbuf *pixbuf);
extern f_gdk_pixbuf_get_pixels gdk_pixbuf_get_pixels;

typedef int (*f_gdk_pixbuf_get_width)(const GdkPixbuf *pixbuf);
extern f_gdk_pixbuf_get_width gdk_pixbuf_get_width;

typedef int (*f_gdk_pixbuf_get_height)(const GdkPixbuf *pixbuf);
extern f_gdk_pixbuf_get_height gdk_pixbuf_get_height;

typedef int (*f_gdk_pixbuf_get_rowstride)(const GdkPixbuf *pixbuf);
extern f_gdk_pixbuf_get_rowstride gdk_pixbuf_get_rowstride;

inline bool GtkSettingSupported() {
	return gtk_settings_get_default != nullptr;
}

template <typename T>
inline T GtkSetting(const gchar *propertyName) {
	GtkSettings *settings = gtk_settings_get_default();
	T value;
	g_object_get(settings, propertyName, &value, nullptr);
	return value;
}

inline QString GtkSetting(const gchar *propertyName) {
	gchararray value = GtkSetting<gchararray>(propertyName);
	QString str = QString::fromUtf8(value);
	g_free(value);
	DEBUG_LOG(("Getting GTK setting, %1: '%2'").arg(propertyName).arg(str));
	return str;
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace Libs
} // namespace Platform
