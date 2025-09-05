/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_star_gift.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
struct GiftCode;
struct CreditsHistoryEntry;
class SavedStarGiftId;
} // namespace Data

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

class PopupMenu;
class GenericBox;
class VerticalLayout;

void ChooseStarGiftRecipient(
	not_null<Window::SessionController*> controller);

void ShowStarGiftBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddUniqueGiftCover(
	not_null<VerticalLayout*> container,
	rpl::producer<Data::UniqueGift> data,
	rpl::producer<QString> subtitleOverride = nullptr,
	rpl::producer<CreditsAmount> resalePrice = nullptr,
	Fn<void()> resaleClick = nullptr);
void AddWearGiftCover(
	not_null<VerticalLayout*> container,
	const Data::UniqueGift &data,
	not_null<PeerData*> peer);

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

void GiftReleasedByHandler(not_null<PeerData*> peer);

struct PatternPoint {
	QPointF position;
	float64 scale = 1.;
	float64 opacity = 1.;
};
[[nodiscard]] const std::vector<PatternPoint> &PatternPoints();
[[nodiscard]] const std::vector<PatternPoint> &PatternPointsSmall();

void PaintPoints(
	QPainter &p,
	const std::vector<PatternPoint> &points,
	base::flat_map<float64, QImage> &cache,
	not_null<Text::CustomEmoji*> emoji,
	const Data::UniqueGift &gift,
	const QRect &rect,
	float64 shown = 1.);

struct StarGiftUpgradeArgs {
	not_null<Window::SessionController*> controller;
	base::required<uint64> stargiftId;
	Fn<void(bool)> ready;
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

void AddUniqueCloseButton(
	not_null<GenericBox*> box,
	Settings::CreditsEntryBoxStyleOverrides st,
	Fn<void(not_null<PopupMenu*>)> fillMenu = nullptr);

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

void ShowResaleGiftBoughtToast(
	std::shared_ptr<Main::SessionShow> show,
	not_null<PeerData*> to,
	const Data::UniqueGift &gift);

[[nodiscard]] rpl::lifetime ShowStarGiftResale(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	uint64 giftId,
	QString title,
	Fn<void()> finishRequesting);

[[nodiscard]] CreditsAmount StarsFromTon(
	not_null<Main::Session*> session,
	CreditsAmount ton);
[[nodiscard]] CreditsAmount TonFromStars(
	not_null<Main::Session*> session,
	CreditsAmount stars);

} // namespace Ui
