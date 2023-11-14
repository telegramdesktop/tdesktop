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

void FillMenu(
		not_null<Window::SessionController*> controller,
		Type type,
		Fn<void(Type)> showOther,
		Ui::Menu::MenuCallback addAction) {
	const auto window = &controller->window();
	if (type == Chat::Id()) {
		addAction(
			tr::lng_settings_bg_theme_create(tr::now),
			[=] { window->show(Box(Window::Theme::CreateBox, window)); },
			&st::menuIconChangeColors);
	} else if (type == CloudPasswordEmailConfirmId()) {
		const auto api = &controller->session().api();
		if (const auto state = api->cloudPassword().stateCurrent()) {
			if (state->unconfirmedPattern.isEmpty()) {
				return;
			}
		}
		addAction(
			tr::lng_settings_password_abort(tr::now),
			[=] { api->cloudPassword().clearUnconfirmedPassword(); },
			&st::menuIconCancel);
	} else {
		const auto &list = Core::App().domain().accounts();
		if (list.size() < Core::App().domain().maxAccounts()) {
			addAction(tr::lng_menu_add_account(tr::now), [=] {
				Core::App().domain().addActivated(MTP::Environment{});
			}, &st::menuIconAddAccount);
		}
		if (!controller->session().supportMode()) {
			addAction(
				tr::lng_settings_information(tr::now),
				[=] { showOther(Information::Id()); },
				&st::menuIconInfo);
		}
		addAction({
			.text = tr::lng_settings_logout(tr::now),
			.handler = [=] { window->showLogoutConfirmation(); },
			.icon = &st::menuIconLeaveAttention,
			.isAttention = true,
		});
	}
}

} // namespace Settings
