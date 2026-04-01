/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_dock.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/text/text_utilities.h"

#include <QtWidgets/QMenu>

namespace Menu {

void RefreshDockMenu(QMenu *menu) {
	menu->clear();
	if (!Core::IsAppLaunched()) {
		return;
	}

	const auto accounts = Core::App().domain().orderedAccounts();
	if (accounts.size() > 1) {
		menu->addSeparator();
		const auto profilesHeader = menu->addAction(
			tr::lng_mac_menu_profiles(tr::now));
		profilesHeader->setEnabled(false);
		constexpr auto kMaxLength = 30;
		for (const auto &account : accounts) {
			if (account->sessionExists()) {
				const auto name = account->session().user()->name();
				menu->addAction(
					(name.size() > kMaxLength)
						? (name.mid(0, kMaxLength) + Ui::kQEllipsis)
						: name,
					[account] {
						Core::App().ensureSeparateWindowFor(account);
					});
			}
		}
		menu->addSeparator();
	}

	menu->addAction(
		Core::App().settings().desktopNotify()
			? tr::lng_disable_notifications_from_tray(tr::now)
			: tr::lng_enable_notifications_from_tray(tr::now),
		[] {
			auto &settings = Core::App().settings();
			settings.setDesktopNotify(!settings.desktopNotify());
		});

	using namespace Media::Player;
	const auto type = instance()->getActiveType();
	const auto state = instance()->getState(type);
	if (!IsStoppedOrStopping(state.state)) {
		menu->addSeparator();
		const auto previous = menu->addAction(
			tr::lng_mac_menu_player_previous(tr::now),
			[] { instance()->previous(); });
		previous->setEnabled(instance()->previousAvailable(type));
		menu->addAction(
			IsPausedOrPausing(state.state)
				? tr::lng_mac_menu_player_resume(tr::now)
				: tr::lng_mac_menu_player_pause(tr::now),
			[] { instance()->playPause(); });
		const auto next = menu->addAction(
			tr::lng_mac_menu_player_next(tr::now),
			[] { instance()->next(); });
		next->setEnabled(instance()->nextAvailable(type));
	}
}

} // namespace Menu
