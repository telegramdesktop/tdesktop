/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

class UserData;

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Wallet {

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateContent(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Main::SessionShow> show);

void FillMenu(
	std::shared_ptr<Main::SessionShow> show,
	const Ui::Menu::MenuCallback &addAction);

[[nodiscard]] bool TransferLinkValid(const QString &url);

void ShowTransferLink(
	std::shared_ptr<Main::SessionShow> show,
	const QString &url);

void ShowSendToUser(
	std::shared_ptr<Main::SessionShow> show,
	not_null<UserData*> user);

[[nodiscard]] rpl::producer<bool> TransactionsShownValue(
	not_null<Main::Session*> session);

} // namespace Wallet
