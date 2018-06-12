/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
extern "C" {
#undef signals
#include <gtk/gtk.h>
#define signals public
} // extern "C"

namespace Platform {
namespace Libs {

void startLibNotify();

constexpr gint NOTIFY_EXPIRES_DEFAULT = -1;
constexpr gint NOTIFY_EXPIRES_NEVER = 0;

struct NotifyNotification;
typedef enum {
        NOTIFY_URGENCY_LOW,
        NOTIFY_URGENCY_NORMAL,
        NOTIFY_URGENCY_CRITICAL,
} NotifyUrgency;

using NotifyActionCallback = void (*)(NotifyNotification *notification, char *action, gpointer user_data);

using f_notify_init = gboolean (*)(const char *app_name);
extern f_notify_init notify_init;

using f_notify_uninit = void (*)(void);
extern f_notify_uninit notify_uninit;

using f_notify_is_initted = gboolean (*)(void);
extern f_notify_is_initted notify_is_initted;

//using f_notify_get_app_name = const char* (*)(void);
//extern f_notify_get_app_name notify_get_app_name;

//using f_notify_set_app_name = void (*)(const char *app_name);
//extern f_notify_set_app_name notify_set_app_name;

using f_notify_get_server_caps = GList* (*)(void);
extern f_notify_get_server_caps notify_get_server_caps;

using f_notify_get_server_info = gboolean (*)(char **ret_name, char **ret_vendor, char **ret_version, char **ret_spec_version);
extern f_notify_get_server_info notify_get_server_info;

using f_notify_notification_new = NotifyNotification* (*)(const char *summary, const char *body, const char *icon);
extern f_notify_notification_new notify_notification_new;

//using f_notify_notification_update = gboolean (*)(NotifyNotification *notification, const char *summary, const char *body, const char *icon);
//extern f_notify_notification_update notify_notification_update;

using f_notify_notification_show = gboolean (*)(NotifyNotification *notification, GError **error);
extern f_notify_notification_show notify_notification_show;

//using f_notify_notification_set_app_name = void (*)(NotifyNotification *notification, const char *app_name);
//extern f_notify_notification_set_app_name notify_notification_set_app_name;

using f_notify_notification_set_timeout = void (*)(NotifyNotification *notification, gint timeout);
extern f_notify_notification_set_timeout notify_notification_set_timeout;

//using f_notify_notification_set_category = void (*)(NotifyNotification *notification, const char *category);
//extern f_notify_notification_set_category notify_notification_set_category;

//using f_notify_notification_set_urgency = void (*)(NotifyNotification *notification, NotifyUrgency urgency);
//extern f_notify_notification_set_urgency notify_notification_set_urgency;

//using f_notify_notification_set_icon_from_pixbuf = void (*)(NotifyNotification *notification, GdkPixbuf *icon);
//extern f_notify_notification_set_icon_from_pixbuf notify_notification_set_icon_from_pixbuf;

using f_notify_notification_set_image_from_pixbuf = void (*)(NotifyNotification *notification, GdkPixbuf *pixbuf);
extern f_notify_notification_set_image_from_pixbuf notify_notification_set_image_from_pixbuf;

//using f_notify_notification_set_hint = void (*)(NotifyNotification *notification, const char *key, GVariant *value);
//extern f_notify_notification_set_hint notify_notification_set_hint;

//using f_notify_notification_set_hint_int32 = void (*)(NotifyNotification *notification, const char *key, gint value);
//extern f_notify_notification_set_hint_int32 notify_notification_set_hint_int32;

//using f_notify_notification_set_hint_uint32 = void (*)(NotifyNotification *notification, const char *key, guint value);
//extern f_notify_notification_set_hint_uint32 notify_notification_set_hint_uint32;

//using f_notify_notification_set_hint_double = void (*)(NotifyNotification *notification, const char *key, gdouble value);
//extern f_notify_notification_set_hint_double notify_notification_set_hint_double;

using f_notify_notification_set_hint_string = void (*)(NotifyNotification *notification, const char *key, const char *value);
extern f_notify_notification_set_hint_string notify_notification_set_hint_string;

//using f_notify_notification_set_hint_byte = void (*)(NotifyNotification *notification, const char *key, guchar value);
//extern f_notify_notification_set_hint_byte notify_notification_set_hint_byte;

//using f_notify_notification_set_hint_byte_array = void (*)(NotifyNotification *notification, const char *key, const guchar *value, gsize len);
//extern f_notify_notification_set_hint_byte_array notify_notification_set_hint_byte_array;

//using f_notify_notification_clear_hints = void (*)(NotifyNotification *notification);
//extern f_notify_notification_clear_hints notify_notification_clear_hints;

using f_notify_notification_add_action = void (*)(NotifyNotification *notification, const char *action, const char *label, NotifyActionCallback callback, gpointer user_data, GFreeFunc free_func);
extern f_notify_notification_add_action notify_notification_add_action;

using f_notify_notification_clear_actions = void (*)(NotifyNotification *notification);
extern f_notify_notification_clear_actions notify_notification_clear_actions;

using f_notify_notification_close = gboolean (*)(NotifyNotification *notification, GError **error);
extern f_notify_notification_close notify_notification_close;

using f_notify_notification_get_closed_reason = gint (*)(const NotifyNotification *notification);
extern f_notify_notification_get_closed_reason notify_notification_get_closed_reason;

} // namespace Libs
} // namespace Platform
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
