/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
struct HistoryServiceGiftOnchainOffer;
struct PreparedServiceText;

namespace Data {
class Media;
} // namespace Data

enum class GiftOnchainOfferState : uchar {
	Open,
	PastExpiry,
	Cancelling,
	Cancelled,
	Declined,
	Expired,
	Settled,
	Accepted,
};

void FillGiftOnchainOffer(
	not_null<HistoryItem*> item,
	const MTPDmessageActionStarGiftOnchainOffer &action);

[[nodiscard]] GiftOnchainOfferState ComputeGiftOnchainOfferState(
	not_null<const HistoryServiceGiftOnchainOffer*> offer,
	TimeId now);
[[nodiscard]] bool GiftOnchainOfferReviewable(
	not_null<const HistoryServiceGiftOnchainOffer*> offer,
	TimeId now);
[[nodiscard]] TimeId GiftOnchainOfferTimeLeft(
	not_null<const HistoryServiceGiftOnchainOffer*> offer,
	TimeId now);
[[nodiscard]] bool CanWithdrawGiftOnchainOffer(
	not_null<const HistoryItem*> item,
	TimeId now);

[[nodiscard]] PreparedServiceText PrepareGiftOnchainOfferText(
	not_null<const HistoryItem*> item);
[[nodiscard]] TextWithEntities GiftOnchainOfferCardText(
	not_null<const HistoryItem*> item,
	TimeId now);
[[nodiscard]] std::unique_ptr<Data::Media> CreateGiftOnchainOfferMedia(
	not_null<HistoryItem*> item);
