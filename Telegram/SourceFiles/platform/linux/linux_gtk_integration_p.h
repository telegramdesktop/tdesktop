/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

extern "C" {
#include <gtk/gtk.h>
#include <gdk/gdk.h>
} // extern "C"

namespace Platform {
namespace Gtk {

inline void (*gtk_widget_show)(GtkWidget *widget) = nullptr;
inline GdkWindow* (*gtk_widget_get_window)(GtkWidget *widget) = nullptr;
inline void (*gtk_widget_realize)(GtkWidget *widget) = nullptr;
inline void (*gtk_widget_destroy)(GtkWidget *widget) = nullptr;
inline GtkClipboard* (*gtk_clipboard_get)(GdkAtom selection) = nullptr;
inline GtkSelectionData* (*gtk_clipboard_wait_for_contents)(GtkClipboard *clipboard, GdkAtom target) = nullptr;
inline GdkPixbuf* (*gtk_clipboard_wait_for_image)(GtkClipboard *clipboard) = nullptr;
inline gboolean (*gtk_selection_data_targets_include_image)(const GtkSelectionData *selection_data, gboolean writable) = nullptr;
inline void (*gtk_selection_data_free)(GtkSelectionData *data) = nullptr;
inline GType (*gtk_app_chooser_get_type)(void) G_GNUC_CONST = nullptr;
inline GtkWidget* (*gtk_app_chooser_dialog_new)(GtkWindow *parent, GtkDialogFlags flags, GFile *file) = nullptr;
inline GAppInfo* (*gtk_app_chooser_get_app_info)(GtkAppChooser *self) = nullptr;
inline GdkAtom (*gdk_atom_intern)(const gchar *atom_name, gboolean only_if_exists) = nullptr;
inline gboolean (*gdk_pixbuf_get_has_alpha)(const GdkPixbuf *pixbuf) = nullptr;
inline guchar* (*gdk_pixbuf_get_pixels)(const GdkPixbuf *pixbuf) = nullptr;
inline int (*gdk_pixbuf_get_width)(const GdkPixbuf *pixbuf) = nullptr;
inline int (*gdk_pixbuf_get_height)(const GdkPixbuf *pixbuf) = nullptr;
inline int (*gdk_pixbuf_get_rowstride)(const GdkPixbuf *pixbuf) = nullptr;

} // namespace Gtk
} // namespace Platform
