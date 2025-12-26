/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct GiftAuctionState;
struct ActiveAuctions;
struct StarGift;
} // namespace Data

namespace Info::PeerGifts {
struct GiftSendDetails;
} // namespace Info::PeerGifts

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class BoxContent;
class RoundButton;
class GenericBox;

[[nodiscard]] rpl::lifetime ShowStarGiftAuction(
	not_null<Window::SessionController*> controller,
	PeerData *peer,
	QString slug,
	Fn<void()> finishRequesting,
	Fn<void()> boxClosed);

struct AuctionBidBoxArgs {
	not_null<PeerData*> peer;
	std::shared_ptr<ChatHelpers::Show> show;
	rpl::producer<Data::GiftAuctionState> state;
	std::unique_ptr<Info::PeerGifts::GiftSendDetails> details;
};
[[nodiscard]] object_ptr<BoxContent> MakeAuctionBidBox(
	AuctionBidBoxArgs &&args);

enum class AuctionButtonCountdownType {
	Join,
	Place,
	Preview,
};
void SetAuctionButtonCountdownText(
	not_null<RoundButton*> button,
	AuctionButtonCountdownType type,
	rpl::producer<Data::GiftAuctionState> value);

void AuctionAboutBox(
	not_null<GenericBox*> box,
	int rounds,
	int giftsPerRound,
	Fn<void(Fn<void()> close)> understood);

[[nodiscard]] TextWithEntities ActiveAuctionsTitle(
	const Data::ActiveAuctions &auctions);
struct ManyAuctionsState {
	TextWithEntities text;
	bool someOutbid = false;
};
[[nodiscard]] ManyAuctionsState ActiveAuctionsState(
	const Data::ActiveAuctions &auctions);
[[nodiscard]] rpl::producer<TextWithEntities> ActiveAuctionsButton(
	const Data::ActiveAuctions &auctions);
[[nodiscard]] Fn<void()> ActiveAuctionsCallback(
	not_null<Window::SessionController*> window,
	const Data::ActiveAuctions &auctions);

} // namespace Ui
