/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class SeparatePanel;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

not_null<Ui::SeparatePanel*> ShowWallet(not_null<Main::Session*> session);

void CloseWallet(not_null<Main::Session*> session);

void OpenTransferLink(
	not_null<Window::SessionController*> controller,
	const QString &url);

} // namespace Wallet
