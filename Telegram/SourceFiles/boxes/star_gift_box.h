/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/star_gift_cover_box.h"
#include "data/data_star_gift.h"

namespace Api {
class PremiumGiftCodeOptions;
} // namespace Api

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
struct GiftCode;
struct CreditsHistoryEntry;
class SavedStarGiftId;
struct GiftAuctionState;
} // namespace Data

namespace Info::PeerGifts {
struct GiftDescriptor;
} // namespace Info::PeerGifts

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Payments {
enum class CheckoutResult;
} // namespace Payments

namespace Settings {
struct GiftWearBoxStyleOverride;
struct CreditsEntryBoxStyleOverrides;
} // namespace Settings

namespace Window {
class SessionController;
} // namespace Window

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui {

class RpWidget;
class PopupMenu;
class GenericBox;
class VerticalLayout;

void ChooseStarGiftRecipient(
	not_null<Window::SessionController*> controller);

void ShowStarGiftBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddWearGiftCover(
	not_null<VerticalLayout*> container,
	const Data::UniqueGift &data,
	not_null<PeerData*> peer);

void AttachGiftSenderBadge(
	not_null<GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> from,
	const QDateTime &date,
	bool crafted);

void ShowUniqueGiftWearBox(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	const Data::UniqueGift &gift,
	Settings::GiftWearBoxStyleOverride st);

void PreloadUniqueGiftResellPrices(not_null<Main::Session*> session);

void UpdateGiftSellPrice(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> unique,
	Data::SavedStarGiftId savedId,
	CreditsAmount price);
void ShowUniqueGiftSellBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> unique,
	Data::SavedStarGiftId savedId,
	Settings::GiftWearBoxStyleOverride st);

void ShowOfferBuyBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<Data::UniqueGift> unique);

struct StarGiftUpgradeArgs {
	not_null<Window::SessionController*> controller;
	Data::StarGift stargift;
	Fn<void(bool)> ready;
	Fn<void()> upgraded;
	not_null<PeerData*> peer;
	Data::SavedStarGiftId savedId;
	QString giftPrepayUpgradeHash;
	int cost = 0;
	bool canAddSender = false;
	bool canAddComment = false;
	bool canAddMyComment = false;
	bool addDetailsDefault = false;
};
void ShowStarGiftUpgradeBox(StarGiftUpgradeArgs &&args);

void SubmitStarsForm(
	std::shared_ptr<Main::SessionShow> show,
	MTPInputInvoice invoice,
	uint64 formId,
	uint64 price,
	Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done);
void SubmitTonForm(
	std::shared_ptr<Main::SessionShow> show,
	MTPInputInvoice invoice,
	uint64 formId,
	CreditsAmount ton,
	Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done);
void RequestOurForm(
	std::shared_ptr<Main::SessionShow> show,
	MTPInputInvoice invoice,
	Fn<void(
		uint64 formId,
		CreditsAmount price,
		std::optional<Payments::CheckoutResult> failure)> done);
void RequestStarsFormAndSubmit(
	std::shared_ptr<Main::SessionShow> show,
	MTPInputInvoice invoice,
	Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done);

void ShowGiftTransferredToast(
	std::shared_ptr<Main::SessionShow> show,
	not_null<PeerData*> to,
	const Data::UniqueGift &gift);

[[nodiscard]] CreditsAmount StarsFromTon(
	not_null<Main::Session*> session,
	CreditsAmount ton);
[[nodiscard]] CreditsAmount TonFromStars(
	not_null<Main::Session*> session,
	CreditsAmount stars);

struct GiftsDescriptor {
	std::vector<Info::PeerGifts::GiftDescriptor> list;
	std::shared_ptr<Api::PremiumGiftCodeOptions> api;
};
enum class GiftsListMode {
	Send,
	Craft,
	CraftResale,
};
struct GiftsListArgs {
	not_null<Window::SessionController*> window;
	GiftsListMode mode = GiftsListMode::Send;
	not_null<PeerData*> peer;
	rpl::producer<GiftsDescriptor> gifts;
	std::vector<std::shared_ptr<Data::UniqueGift>> selected;
	Fn<void()> loadMore;
	Fn<void(Info::PeerGifts::GiftDescriptor)> handler;
};
[[nodiscard]] object_ptr<RpWidget> MakeGiftsList(GiftsListArgs &&args);

void SendGiftBox(
	not_null<GenericBox*> box,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	std::shared_ptr<Api::PremiumGiftCodeOptions> api,
	const Info::PeerGifts::GiftDescriptor &descriptor,
	rpl::producer<Data::GiftAuctionState> auctionState);

[[nodiscard]] Data::CreditsHistoryEntry EntryForUpgradedGift(
	const std::shared_ptr<Data::GiftUpgradeResult> &gift,
	uint64 nextToUpgradeStickerId = 0,
	Fn<void()> nextToUpgradeShow = nullptr,
	Fn<void()> craftAnother = nullptr);

[[nodiscard]] std::shared_ptr<Data::GiftUpgradeResult> FindUniqueGift(
	not_null<Main::Session*> session,
	const MTPUpdates &updates);

} // namespace Ui
