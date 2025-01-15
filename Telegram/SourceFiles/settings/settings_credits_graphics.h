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
struct ShareBoxStyleOverrides;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct Boost;
struct CreditsHistoryEntry;
struct SubscriptionEntry;
struct GiftCode;
struct CreditTopupOption;
struct SavedStarGift;
struct StarGift;
} // namespace Data

namespace HistoryView {
struct ScheduleBoxStyleArgs;
} // namespace HistoryView

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct Box;
struct Table;
struct FlatLabel;
struct PopupMenu;
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
	StarsAmount minCredits,
	Fn<void()> paid,
	rpl::producer<QString> subtitle,
	std::vector<Data::CreditTopupOption> preloadedTopupOptions);

[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<StarsAmount> balanceValue,
	bool rightAlign,
	rpl::producer<float64> opacityValue = nullptr);

void AddWithdrawalWidget(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	rpl::producer<QString> secondButtonUrl,
	rpl::producer<StarsAmount> availableBalanceValue,
	rpl::producer<QDateTime> dateValue,
	bool withdrawalEnabled,
	rpl::producer<QString> usdValue);

struct GiftWearBoxStyleOverride {
	const style::Box *box = nullptr;
	const style::FlatLabel *title = nullptr;
	const style::FlatLabel *subtitle = nullptr;
	const style::icon *radiantIcon = nullptr;
	const style::icon *proofIcon = nullptr;
	const style::FlatLabel *infoTitle = nullptr;
	const style::FlatLabel *infoAbout = nullptr;
};
[[nodiscard]] GiftWearBoxStyleOverride DarkGiftWearBoxStyle();

struct CreditsEntryBoxStyleOverrides {
	const style::Box *box = nullptr;
	const style::PopupMenu *menu = nullptr;
	const style::Table *table = nullptr;
	const style::FlatLabel *tableValueMultiline = nullptr;
	const style::FlatLabel *tableValueMessage = nullptr;
	const style::icon *link = nullptr;
	const style::icon *share = nullptr;
	const style::icon *transfer = nullptr;
	const style::icon *wear = nullptr;
	const style::icon *takeoff = nullptr;
	std::shared_ptr<ShareBoxStyleOverrides> shareBox;
	std::shared_ptr<GiftWearBoxStyleOverride> giftWearBox;
};
[[nodiscard]] CreditsEntryBoxStyleOverrides DarkCreditsEntryBoxStyle();

void GenericCreditsEntryBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::CreditsHistoryEntry &e,
	const Data::SubscriptionEntry &s,
	CreditsEntryBoxStyleOverrides st = {});
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
void GlobalStarGiftBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::StarGift &data,
	CreditsEntryBoxStyleOverrides st = {});
void SavedStarGiftBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> owner,
	const Data::SavedStarGift &data);
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

[[nodiscard]] object_ptr<Ui::RpWidget> SubscriptionUserpic(
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer,
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
	PeerId recipientId;
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
	uint64 wholeCredits,
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

void AddMiniStars(
	not_null<Ui::VerticalLayout*> content,
	not_null<Ui::RpWidget*> widget,
	int photoSize,
	int boxWidth,
	float64 heightRatio);

} // namespace Settings
