/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_panel.h"

#include "base/qthelp_regex.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_link.h"
#include "wallet/wallet_ton_connect_request.h"
#include "wallet/wallet_user_addresses.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

[[nodiscard]] Ui::SeparatePanel *ActivePanel() {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto panel = account->session().wallet().panel();
		if (panel && panel->isActiveWindow()) {
			return panel;
		}
	}
	return nullptr;
}

// A link amount counts Grams with a dot; a comma is refused.
[[nodiscard]] std::optional<int64> ParseLinkAmount(const QString &amount) {
	if (amount.isEmpty()) {
		return 0;
	} else if (!qthelp::regex_match(u"^\\d+(\\.\\d+)?$"_q, amount, {})) {
		return std::nullopt;
	}
	return Ui::ParseTonAmountString(amount, u"."_q);
}

} // namespace

not_null<Ui::SeparatePanel*> ShowWallet(not_null<Main::Session*> session) {
	auto &wallet = session->wallet();
	wallet.tonConnect().refreshSessions();
	if (const auto exists = wallet.panel()) {
		const auto state = exists->windowState();
		if (state & Qt::WindowMinimized) {
			exists->setWindowState(state & ~Qt::WindowMinimized);
		}
		exists->showAndActivate();
		return exists;
	}
	auto owned = std::make_unique<Ui::SeparatePanel>();
	const auto panel = owned.get();
	UseWindowPalette(panel);
	const auto show = Main::MakeSessionShow(panel->uiShow(), session);
	panel->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	Shortcuts::Listen(panel); // Main window may be hidden to tray.
	panel->setInnerSize(st::walletPanelSize);
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(panel);
		panel->overrideTitleColor(st::windowBgOver->c);
		panel->overrideBottomBarColor(st::windowBgOver->c);
		panel->overrideBodyColor(WindowPalette()
			? std::make_optional(st::windowBg->c)
			: std::nullopt);
	}, panel->lifetime());
	TransactionsShownValue(
		session
	) | rpl::on_next([=](bool shown) {
		panel->setBottomBarHeight(shown ? st::walletRowsHintHeight : 0);
	}, panel->lifetime());
	panel->setMenuAllowed([=](const Ui::Menu::MenuCallback &addAction) {
		FillMenu(show, addAction);
	});

	panel->windowActiveValue(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		session->wallet().tonConnect().refreshSessions();
	}, panel->lifetime());

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
	if (const auto panel = ActivePanel()) {
		panel->close();
		return true;
	}
	return false;
}

bool MinimizeActiveWindow() {
	if (const auto panel = ActivePanel()) {
		panel->setWindowState(panel->windowState() | Qt::WindowMinimized);
		return true;
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

void OpenSendGramsLink(
		not_null<Window::SessionController*> controller,
		const QString &to,
		const QString &amount) {
	const auto amountNano = ParseLinkAmount(amount);
	if (!amountNano || (to.isEmpty() && !amount.isEmpty())) {
		controller->showToast(tr::lng_wallet_send_link_invalid(tr::now));
		return;
	}
	const auto session = &controller->session();
	const auto panel = ShowWallet(session);
	if (to.isEmpty()) {
		return;
	}
	ShowSendToLinkRecipient(
		Main::MakeSessionShow(panel->uiShow(), session),
		to,
		*amountNano);
}

void OpenAddressEntity(
		not_null<Window::SessionController*> controller,
		const QString &text) {
	const auto address = text.trimmed();
	if (!ParseAddress(address)) {
		controller->showToast(tr::lng_wallet_send_link_invalid(tr::now));
		return;
	}
	OpenSendGramsLink(controller, address, QString());
}

bool CanOfferSendMoney(not_null<UserData*> user) {
	const auto session = &user->session();
	auto &wallet = session->wallet();
	return !session->supportMode()
		&& session->appConfig().walletAvailable()
		&& (wallet.presenceCurrent() != Presence::Unavailable)
		&& wallet.userAddresses().recipientError(
			peerToUser(user->id)).isEmpty();
}

void OpenSendMoney(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		Fn<void()> sent) {
	const auto session = &controller->session();
	if (&user->session() != session || !CanOfferSendMoney(user)) {
		return;
	}
	const auto weak = base::make_weak(session);
	ShowSendToUser(
		controller->uiShow(),
		user,
		std::move(sent),
		0,
		[=] {
			if (const auto strong = weak.get()) {
				ShowWallet(strong);
			}
		});
}

void OpenTonConnectLink(
		not_null<Window::SessionController*> controller,
		const TonConnectLink &link) {
	auto &tonConnect = controller->session().wallet().tonConnect();
	if (link.kind == TonConnectLinkKind::OpenPending) {
		tonConnect.requests().openPending(controller, link.clientId);
	} else if (link.kind == TonConnectLinkKind::Connect) {
		tonConnect.connect(controller, link);
	} else {
		ShowWallet(&controller->session());
	}
}

void OpenTonConnectRequest(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	controller->session().wallet().tonConnect().requests().open(
		controller,
		itemId);
}

std::shared_ptr<Main::SessionShow> TonConnectBoxShow(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	if (session->wallet().panel()) {
		ShowWallet(session);
	} else {
		controller->window().activate();
	}
	return TonConnectBoxShowNoActivate(controller);
}

std::shared_ptr<Main::SessionShow> TonConnectBoxShowNoActivate(
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	if (const auto panel = session->wallet().panel()) {
		return Main::MakeSessionShow(panel->uiShow(), session);
	}
	return controller->uiShow();
}

std::shared_ptr<Main::SessionShow> ActiveWindowShow(
		not_null<Main::Session*> session) {
	const auto panel = session->wallet().panel();
	return (panel && panel->isActiveWindow())
		? Main::MakeSessionShow(panel->uiShow(), session)
		: nullptr;
}

} // namespace Wallet
