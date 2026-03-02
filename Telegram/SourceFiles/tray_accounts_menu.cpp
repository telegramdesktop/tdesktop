/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tray_accounts_menu.h"

#include "base/weak_ptr.h"
#include "base/qt/qt_key_modifiers.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "styles/style_window.h"
#include "ui/dynamic_thumbnails.h"

namespace Core::TrayAccountsMenu {

void SetupChangesSubscription(Fn<void()> callback, rpl::lifetime &lifetime) {
	const auto accountSessionsLifetime = lifetime.make_state<rpl::lifetime>();
	const auto watchAccountSessions = [=] {
		accountSessionsLifetime->destroy();
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			account->sessionChanges(
			) | rpl::on_next([=](Main::Session*) {
				callback();
			}, *accountSessionsLifetime);
		}
	};
	Core::App().domain().accountsChanges() | rpl::on_next([=] {
		watchAccountSessions();
		callback();
	}, lifetime);
	watchAccountSessions();
}

void Fill(Platform::Tray &tray) {
	auto accounts = std::vector<not_null<Main::Account*>>();
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()) {
			accounts.push_back(account);
		}
	}
	if (accounts.size() <= 1) {
		return;
	}
	tray.addSeparator();
	constexpr auto kMaxLength = 30;
	for (const auto account : accounts) {
		const auto user = account->session().user();
		const auto weak = base::make_weak(account);
		tray.addAction(
			Info::Profile::NameValue(
				user
			) | rpl::map([=](const QString &name) {
				return (name.size() > kMaxLength)
					? (name.mid(0, kMaxLength) + Ui::kQEllipsis)
					: name;
			}),
			[weak] {
				const auto strong = weak.get();
				if (!strong || !strong->sessionExists()) {
					return;
				}
				if (base::IsCtrlPressed()) {
					Core::App().ensureSeparateWindowFor({ strong });
				} else {
					Core::App().domain().maybeActivate(strong);
				}
			},
			Ui::MakeUserpicThumbnail(user, true),
			st::notifyMacPhotoSize);
	}
}

} // namespace Core::TrayAccountsMenu
