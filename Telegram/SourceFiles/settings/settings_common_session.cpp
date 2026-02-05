/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common_session.h"

#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_main.h"

namespace Settings {

bool HasMenu(Type type) {
	return (type == ::Settings::CloudPasswordEmailConfirmId())
		|| (type == MainId())
		|| (type == ChatId());
}

} // namespace Settings
