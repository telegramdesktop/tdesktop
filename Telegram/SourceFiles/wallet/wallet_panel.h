/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct FullMsgId;
class UserData;

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

struct TonConnectLink;

not_null<Ui::SeparatePanel*> ShowWallet(not_null<Main::Session*> session);

void CloseWallet(not_null<Main::Session*> session);

bool CloseActiveWindow();
bool MinimizeActiveWindow();

void OpenTransferLink(
	not_null<Window::SessionController*> controller,
	const QString &url);

void OpenSendGramsLink(
	not_null<Window::SessionController*> controller,
	const QString &to,
	const QString &amount);

void OpenAddressEntity(
	not_null<Window::SessionController*> controller,
	const QString &text);

[[nodiscard]] bool CanOfferSendMoney(not_null<UserData*> user);

// Without a ready wallet to send from this opens the wallet panel instead.
void OpenSendMoney(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	Fn<void()> sent = nullptr);

void OpenTonConnectLink(
	not_null<Window::SessionController*> controller,
	const TonConnectLink &link);

void OpenTonConnectRequest(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId);

[[nodiscard]] std::shared_ptr<Main::SessionShow> TonConnectBoxShow(
	not_null<Window::SessionController*> controller);

[[nodiscard]] std::shared_ptr<Main::SessionShow> TonConnectBoxShowNoActivate(
	not_null<Window::SessionController*> controller);

// The wallet window while it is the active one, so that a server popup
// answering something done there is seen there.
[[nodiscard]] std::shared_ptr<Main::SessionShow> ActiveWindowShow(
	not_null<Main::Session*> session);

} // namespace Wallet
