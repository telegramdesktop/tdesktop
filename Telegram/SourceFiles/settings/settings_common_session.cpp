/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common_session.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/settings_advanced.h"
#include "settings/settings_calls.h"
#include "settings/settings_chat.h"
#include "settings/settings_experimental.h"
#include "settings/settings_folders.h"
#include "settings/settings_information.h"
#include "settings/settings_main.h"
#include "settings/settings_notifications.h"
#include "settings/settings_privacy_security.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

#include <QAction>

namespace Settings {

bool HasMenu(Type type) {
	return (type == ::Settings::CloudPasswordEmailConfirmId())
		|| (type == Main::Id())
		|| (type == Chat::Id());
}

} // namespace Settings
