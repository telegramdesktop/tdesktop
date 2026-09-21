/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

void OpenTonConnectLink(
	not_null<Window::SessionController*> controller,
	const TonConnectLink &link);

[[nodiscard]] std::shared_ptr<Main::SessionShow> TonConnectBoxShow(
	not_null<Window::SessionController*> controller);

[[nodiscard]] std::shared_ptr<Main::SessionShow> TonConnectBoxShowNoActivate(
	not_null<Window::SessionController*> controller);

} // namespace Wallet
