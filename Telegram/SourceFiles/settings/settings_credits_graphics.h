/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

class PeerData;

namespace Api {
struct UserStarGift;
} // namespace Api

namespace Data {
struct Boost;
struct CreditsHistoryEntry;
struct SubscriptionEntry;
struct GiftCode;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct PeerListItem;
} // namespace style

namespace Ui {
class GenericBox;
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Settings {

struct SubscriptionRightLabel {
	Fn<void(QPainter &, int x, int y, int h)> draw;
	QSize size;
};
SubscriptionRightLabel PaintSubscriptionRightLabelCallback(
	not_null<Main::Session*> session,
	const style::PeerListItem &st,
	int amount);

void FillCreditOptions(
	std::shared_ptr<Main::SessionShow> show,
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	int minCredits,
	Fn<void()> paid);

[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<uint64> balanceValue,
	bool rightAlign);

void AddWithdrawalWidget(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	rpl::producer<QString> secondButtonUrl,
	rpl::producer<uint64> availableBalanceValue,
	rpl::producer<QDateTime> dateValue,
	rpl::producer<bool> lockedValue,
	rpl::producer<QString> usdValue);

void ReceiptCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const Data::CreditsHistoryEntry &e,
	const Data::SubscriptionEntry &s);
void BoostCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const Data::Boost &b);
void GiftedCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> from,
	not_null<PeerData*> to,
	int count,
	TimeId date);
void CreditsPrizeBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const Data::GiftCode &data,
	TimeId date);
void UserStarGiftBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const Api::UserStarGift &data);
void StarGiftViewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const Data::GiftCode &data,
	not_null<HistoryItem*> item);
void ShowRefundInfoBox(
	not_null<Window::SessionController*> controller,
	FullMsgId refundItemId);

[[nodiscard]] object_ptr<Ui::RpWidget> GenericEntryPhoto(
	not_null<Ui::RpWidget*> parent,
	Fn<Fn<void(Painter &, int, int, int, int)>(Fn<void()> update)> callback,
	int photoSize);

[[nodiscard]] object_ptr<Ui::RpWidget> HistoryEntryPhoto(
	not_null<Ui::RpWidget*> parent,
	not_null<PhotoData*> photo,
	int photoSize);

[[nodiscard]] object_ptr<Ui::RpWidget> PaidMediaThumbnail(
	not_null<Ui::RpWidget*> parent,
	not_null<PhotoData*> photo,
	PhotoData *second,
	int totalCount,
	int photoSize);

struct SmallBalanceBot {
	UserId botId = 0;
};
struct SmallBalanceReaction {
	ChannelId channelId = 0;
};
struct SmallBalanceSubscription {
	QString name;
};
struct SmallBalanceDeepLink {
	QString purpose;
};
struct SmallBalanceStarGift {
	UserId userId = 0;
};
struct SmallBalanceSource : std::variant<
	SmallBalanceBot,
	SmallBalanceReaction,
	SmallBalanceSubscription,
	SmallBalanceDeepLink,
	SmallBalanceStarGift> {
	using variant::variant;
};

void SmallBalanceBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	uint64 credits,
	SmallBalanceSource source,
	Fn<void()> paid);

enum class SmallBalanceResult {
	Already,
	Success,
	Blocked,
	Cancelled,
};

void MaybeRequestBalanceIncrease(
	std::shared_ptr<Main::SessionShow> show,
	uint64 credits,
	SmallBalanceSource source,
	Fn<void(SmallBalanceResult)> done);

} // namespace Settings

