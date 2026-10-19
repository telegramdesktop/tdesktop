/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "ui/layers/layer_widget.h"

class UserData;

namespace style {
struct FlatLabel;
} // namespace style

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class FlatLabel;
class RpWidget;
class SeparatePanel;
class Show;
class TableLayout;
class VerticalLayout;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Wallet {

struct CardLook;
class CommentScope;
class CollectibleMedia;
class WindowPalette;
struct KeyAuthorization;
struct TransferItem;
enum class SendError;

void AcquireTransferCommentKey(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CommentScope> scope,
	Fn<bool()> current,
	rpl::lifetime &lifetime,
	Fn<void(KeyAuthorization)> done);

// The wallet key ladder of a send press, for a caller that owns no key context.
void AcquireWalletKey(
	std::shared_ptr<Main::SessionShow> show,
	Fn<bool()> current,
	rpl::lifetime &lifetime,
	Fn<void(KeyAuthorization)> done,
	rpl::producer<QString> importAbout = nullptr);

void ShowTransactionDetails(
	std::shared_ptr<Main::SessionShow> show,
	TransferItem item,
	bool partial = false,
	std::shared_ptr<CollectibleMedia> media = nullptr,
	Fn<bool()> originCurrent = nullptr,
	rpl::producer<> originInvalidated = nullptr,
	Fn<void()> openWallet = nullptr,
	Ui::LayerOptions options = Ui::LayerOption::KeepOther);

void ShowSubmittedTransfer(
	std::shared_ptr<Main::SessionShow> show,
	const std::string &operationId);

bool ShowFirstGramsIfPending(std::shared_ptr<Main::SessionShow> show);

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateContent(
	not_null<Ui::SeparatePanel*> panel,
	std::shared_ptr<Main::SessionShow> show,
	const std::shared_ptr<WindowPalette> &palette);

struct WalletCardArgs {
	rpl::producer<TextWithEntities> name;
	rpl::producer<QString> address;
	rpl::producer<int64> balanceNano;
	rpl::producer<QString> fiat;
	rpl::producer<CardLook> look;
};

[[nodiscard]] object_ptr<Ui::RpWidget> MakeWalletCard(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<bool> markPlayed,
	WalletCardArgs args = {});

struct TransferCardArgs {
	int64 totalNano = 0;
	std::optional<int64> netNano; // replaces the shown -totalNano when set
	QString destination;
	int recipients = 0;
	Fn<void()> info;
	std::shared_ptr<bool> markPlayed; // one per box: a rebuilt card won't replay
};

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTransferCard(
	QWidget *parent,
	not_null<Main::Session*> session,
	TransferCardArgs args);

enum class ActionRowIcon : uchar {
	Incoming,
	Outgoing,
	Gear,
};

enum class ActionRowSign : uchar {
	None,
	Plus,
	Minus,
};

struct ActionRowArgs {
	QString kind;
	QString address;
	std::optional<int64> amountNano;
	ActionRowSign sign = ActionRowSign::None;
	ActionRowIcon icon = ActionRowIcon::Gear;
};

[[nodiscard]] object_ptr<Ui::RpWidget> MakeActionRow(
	not_null<QWidget*> parent,
	ActionRowArgs args);

[[nodiscard]] Fn<void()> CopyTextCallback(
	std::shared_ptr<Ui::Show> show,
	QString text,
	QString toast);

[[nodiscard]] object_ptr<Ui::FlatLabel> AddressValueLabel(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	const QString &address,
	const style::FlatLabel *styleOverride = nullptr);

// The address in groups of four on a rounded plate, a spinner until known.
void AddAddressPlate(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> address,
	const style::margins &margin,
	int groupsPerLine,
	std::shared_ptr<Ui::Show> show = nullptr,
	rpl::producer<bool> spinnerAllowed = rpl::single(true));

[[nodiscard]] object_ptr<Ui::RpWidget> MakeCommentBubble(
	not_null<QWidget*> parent,
	object_ptr<Ui::RpWidget> content,
	const style::color &bg);

[[nodiscard]] not_null<Ui::TableLayout*> AddDetailsTableFrame(
	not_null<Ui::VerticalLayout*> container);

void FillMenu(
	std::shared_ptr<Main::SessionShow> show,
	const Ui::Menu::MenuCallback &addAction);

[[nodiscard]] bool TransferLinkValid(const QString &url);

void ShowTransferLink(
	std::shared_ptr<Main::SessionShow> show,
	const QString &url);

// After a definite success |sent| runs in place of the transaction
// details box; a caller that passes nothing keeps the details box.
// Without a ready wallet |notReady| runs in place of the error.
// The |origin| box stays below and is closed only once the user leaves.
void ShowSendToUser(
	std::shared_ptr<Main::SessionShow> show,
	not_null<UserData*> user,
	Fn<void()> sent = nullptr,
	int64 amountNano = 0,
	Fn<void()> notReady = nullptr,
	base::weak_qptr<Ui::BoxContent> origin = nullptr);

struct CollectibleTransfer {
	QString address;
	QString slug;
	std::shared_ptr<Data::UniqueGift> unique;
	std::shared_ptr<CollectibleMedia> media;
};

// The recipient step and the confirmation of a collectible's Transfer.
void ShowCollectibleTransfer(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media,
	const QString &collectible,
	const QString &slug = QString(),
	std::shared_ptr<Data::UniqueGift> unique = nullptr);

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

[[nodiscard]] QString SendErrorText(SendError error, int64 minTransferNano);
void ShowWalletKeyChanged(std::shared_ptr<Main::SessionShow> show);
void ShowWalletHowItWorks(std::shared_ptr<Main::SessionShow> show);

// The message, then the error type a real user's report should carry.
[[nodiscard]] QString ErrorWithType(
	const QString &message,
	const QString &error);

} // namespace Wallet
