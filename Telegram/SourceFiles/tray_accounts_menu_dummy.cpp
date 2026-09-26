/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tray_accounts_menu.h"

namespace Core::TrayAccountsMenu {

void SetupChangesSubscription(
		[[maybe_unused]] Fn<void()> callback,
		[[maybe_unused]] rpl::lifetime &lifetime) {
}

void Fill([[maybe_unused]] Platform::Tray &tray) {
}

} // namespace Core::TrayAccountsMenu
