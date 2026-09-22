/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

class UserData;

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class RpWidget;
class SeparatePanel;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Wallet {

class CommentScope;
class CollectibleMedia;
struct KeyAuthorization;
struct TransferItem;

void AcquireTransferCommentKey(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CommentScope> scope,
	Fn<bool()> current,
	rpl::lifetime &lifetime,
	Fn<void(KeyAuthorization)> done);

void ShowTransactionDetails(
	std::shared_ptr<Main::SessionShow> show,
	TransferItem item,
	bool partial = false,
	std::shared_ptr<CollectibleMedia> media = nullptr,
	Fn<bool()> originCurrent = nullptr,
	rpl::producer<> originInvalidated = nullptr,
	Fn<void()> openWallet = nullptr);

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateContent(
	not_null<Ui::SeparatePanel*> panel,
	std::shared_ptr<Main::SessionShow> show);

[[nodiscard]] object_ptr<Ui::RpWidget> MakeWalletCard(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show);

void FillMenu(
	std::shared_ptr<Main::SessionShow> show,
	const Ui::Menu::MenuCallback &addAction);

[[nodiscard]] bool TransferLinkValid(const QString &url);

void ShowTransferLink(
	std::shared_ptr<Main::SessionShow> show,
	const QString &url);

// After a definite success |sent| runs in place of the transaction
// details box; a caller that passes nothing keeps the details box.
void ShowSendToUser(
	std::shared_ptr<Main::SessionShow> show,
	not_null<UserData*> user,
	Fn<void()> sent = nullptr,
	int64 amountNano = 0);

void ShowSendToLinkRecipient(
	std::shared_ptr<Main::SessionShow> show,
	const QString &recipient,
	int64 amountNano);

// The box that lists the wallets parked on this device. Dropping one calls
// switched, so the action that hit the conflict can run again.
void ShowWalletConflict(
	std::shared_ptr<Main::SessionShow> show,
	Fn<void()> switched);

// An inform box with the text, a spinner under it and a Cancel button, for
// an action with no surface of its own to show progress on. Returns the
// call that closes it; a close by the user, Cancel included, instead
// reports through dismissed.
[[nodiscard]] Fn<void()> ShowWalletBusyBox(
	std::shared_ptr<Main::SessionShow> show,
	rpl::producer<QString> text,
	Fn<void()> dismissed);

[[nodiscard]] rpl::producer<bool> TransactionsShownValue(
	not_null<Main::Session*> session);

} // namespace Wallet
