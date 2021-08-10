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
inline GType (*gtk_app_chooser_get_type)(void) G_GNUC_CONST = nullptr;
inline GtkWidget* (*gtk_app_chooser_dialog_new)(GtkWindow *parent, GtkDialogFlags flags, GFile *file) = nullptr;
inline GAppInfo* (*gtk_app_chooser_get_app_info)(GtkAppChooser *self) = nullptr;

} // namespace Gtk
} // namespace Platform
