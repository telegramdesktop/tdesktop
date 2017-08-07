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
#include "platform/linux/linux_libnotify.h"

#include "platform/linux/linux_libs.h"

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

} // namespace

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
f_notify_init notify_init = nullptr;
f_notify_uninit notify_uninit = nullptr;
f_notify_is_initted notify_is_initted = nullptr;
//f_notify_get_app_name notify_get_app_name = nullptr;
//f_notify_set_app_name notify_set_app_name = nullptr;
f_notify_get_server_caps notify_get_server_caps = nullptr;
f_notify_get_server_info notify_get_server_info = nullptr;

f_notify_notification_new notify_notification_new = nullptr;
//f_notify_notification_update notify_notification_update = nullptr;
f_notify_notification_show notify_notification_show = nullptr;
//f_notify_notification_set_app_name notify_notification_set_app_name = nullptr;
f_notify_notification_set_timeout notify_notification_set_timeout = nullptr;
//f_notify_notification_set_category notify_notification_set_category = nullptr;
//f_notify_notification_set_urgency notify_notification_set_urgency = nullptr;
//f_notify_notification_set_icon_from_pixbuf notify_notification_set_icon_from_pixbuf = nullptr;
f_notify_notification_set_image_from_pixbuf notify_notification_set_image_from_pixbuf = nullptr;
//f_notify_notification_set_hint notify_notification_set_hint = nullptr;
//f_notify_notification_set_hint_int32 notify_notification_set_hint_int32 = nullptr;
//f_notify_notification_set_hint_uint32 notify_notification_set_hint_uint32 = nullptr;
//f_notify_notification_set_hint_double notify_notification_set_hint_double = nullptr;
f_notify_notification_set_hint_string notify_notification_set_hint_string = nullptr;
//f_notify_notification_set_hint_byte notify_notification_set_hint_byte = nullptr;
//f_notify_notification_set_hint_byte_array notify_notification_set_hint_byte_array = nullptr;
//f_notify_notification_clear_hints notify_notification_clear_hints = nullptr;
f_notify_notification_add_action notify_notification_add_action = nullptr;
f_notify_notification_clear_actions notify_notification_clear_actions = nullptr;
f_notify_notification_close notify_notification_close = nullptr;
f_notify_notification_get_closed_reason notify_notification_get_closed_reason = nullptr;

void startLibNotify() {
	DEBUG_LOG(("Loading libnotify"));

	QLibrary lib_notify;
	if (!loadLibrary(lib_notify, "notify", 4)) {
		if (!loadLibrary(lib_notify, "notify", 5)) {
			if (!loadLibrary(lib_notify, "notify", 1)) {
				return;
			}
		}
	}

	load(lib_notify, "notify_init", notify_init);
	load(lib_notify, "notify_uninit", notify_uninit);
	load(lib_notify, "notify_is_initted", notify_is_initted);
//	load(lib_notify, "notify_get_app_name", notify_get_app_name);
//	load(lib_notify, "notify_set_app_name", notify_set_app_name);
	load(lib_notify, "notify_get_server_caps", notify_get_server_caps);
	load(lib_notify, "notify_get_server_info", notify_get_server_info);

	load(lib_notify, "notify_notification_new", notify_notification_new);
//	load(lib_notify, "notify_notification_update", notify_notification_update);
	load(lib_notify, "notify_notification_show", notify_notification_show);
//	load(lib_notify, "notify_notification_set_app_name", notify_notification_set_app_name);
	load(lib_notify, "notify_notification_set_timeout", notify_notification_set_timeout);
//	load(lib_notify, "notify_notification_set_category", notify_notification_set_category);
//	load(lib_notify, "notify_notification_set_urgency", notify_notification_set_urgency);
//	load(lib_notify, "notify_notification_set_icon_from_pixbuf", notify_notification_set_icon_from_pixbuf);
	load(lib_notify, "notify_notification_set_image_from_pixbuf", notify_notification_set_image_from_pixbuf);
//	load(lib_notify, "notify_notification_set_hint", notify_notification_set_hint);
//	load(lib_notify, "notify_notification_set_hint_int32", notify_notification_set_hint_int32);
//	load(lib_notify, "notify_notification_set_hint_uint32", notify_notification_set_hint_uint32);
//	load(lib_notify, "notify_notification_set_hint_double", notify_notification_set_hint_double);
	load(lib_notify, "notify_notification_set_hint_string", notify_notification_set_hint_string);
//	load(lib_notify, "notify_notification_set_hint_byte", notify_notification_set_hint_byte);
//	load(lib_notify, "notify_notification_set_hint_byte_array", notify_notification_set_hint_byte_array);
//	load(lib_notify, "notify_notification_clear_hints", notify_notification_clear_hints);
	load(lib_notify, "notify_notification_add_action", notify_notification_add_action);
	load(lib_notify, "notify_notification_clear_actions", notify_notification_clear_actions);
	load(lib_notify, "notify_notification_close", notify_notification_close);
	load(lib_notify, "notify_notification_get_closed_reason", notify_notification_get_closed_reason);
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace Libs
} // namespace Platform
