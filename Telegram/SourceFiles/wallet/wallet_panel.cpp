/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_panel.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

namespace Wallet {

not_null<Ui::SeparatePanel*> ShowWallet(not_null<Main::Session*> session) {
	auto &wallet = session->wallet();
	if (const auto exists = wallet.panel()) {
		exists->showAndActivate();
		return exists;
	}
	auto owned = std::make_unique<Ui::SeparatePanel>();
	const auto panel = owned.get();
	const auto show = Main::MakeSessionShow(panel->uiShow(), session);
	panel->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	panel->setInnerSize(st::walletPanelSize);
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		panel->overrideTitleColor(st::windowBgOver->c);
		panel->overrideBottomBarColor(st::windowBgOver->c);
	}, panel->lifetime());
	TransactionsShownValue(
		session
	) | rpl::on_next([=](bool shown) {
		panel->setBottomBarHeight(shown ? st::walletRowsHintHeight : 0);
	}, panel->lifetime());
	panel->setMenuAllowed([=](const Ui::Menu::MenuCallback &addAction) {
		FillMenu(show, addAction);
	});

	panel->closeRequests(
	) | rpl::on_next([=] {
		panel->hideGetDuration();
	}, panel->lifetime());

	panel->closeEvents(
	) | rpl::on_next([=] {
		session->wallet().setPanel(nullptr);
	}, panel->lifetime());

	wallet.setPanel(std::move(owned));
	panel->showInner(CreateContent(panel, show));
	return panel;
}

void CloseWallet(not_null<Main::Session*> session) {
	session->wallet().setPanel(nullptr);
}

bool CloseActiveWindow() {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto panel = account->session().wallet().panel();
		if (panel && panel->isActiveWindow()) {
			panel->close();
			return true;
		}
	}
	return false;
}

void OpenTransferLink(
		not_null<Window::SessionController*> controller,
		const QString &url) {
	if (!TransferLinkValid(url)) {
		controller->showToast(tr::lng_wallet_send_invalid_address(tr::now));
		return;
	}
	const auto link = ParseTransferLink(url);
	if (link && TransferLinkExpired(link->expiresAt)) {
		controller->showToast(tr::lng_wallet_send_link_expired(tr::now));
		return;
	}
	const auto session = &controller->session();
	const auto panel = ShowWallet(session);
	ShowTransferLink(Main::MakeSessionShow(panel->uiShow(), session), url);
}

} // namespace Wallet
