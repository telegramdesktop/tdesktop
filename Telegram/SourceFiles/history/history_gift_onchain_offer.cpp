/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_gift_onchain_offer.h"

#include "api/api_premium.h"
#include "base/unixtime.h"
#include "data/data_media_types.h"
#include "data/data_star_gift.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"

namespace {

using State = GiftOnchainOfferState;

[[nodiscard]] std::optional<int64> ServedNanos(
		tl::conditional<MTPlong> value) {
	if (!value) {
		return std::nullopt;
	}
	return int64(value->v);
}

[[nodiscard]] TextWithEntities AmountText(int64 nanos) {
	return tr::bold(Data::FormatGiftGramAmountWithUnit(nanos));
}

[[nodiscard]] TextWithEntities GiftNameText(
		not_null<const HistoryServiceGiftOnchainOffer*> offer) {
	return tr::bold(Data::UniqueGiftName(*offer->gift));
}

[[nodiscard]] QString TimeLeftText(TimeId left) {
	constexpr auto kZero = QChar('0');
	return u"%1:%2:%3"_q
		.arg(left / 3600, 2, 10, kZero)
		.arg((left % 3600) / 60, 2, 10, kZero)
		.arg(left % 60, 2, 10, kZero);
}

[[nodiscard]] TextWithEntities OfferText(
		not_null<const HistoryItem*> item,
		not_null<const HistoryServiceGiftOnchainOffer*> offer) {
	if (offer->buyer) {
		return tr::lng_action_gift_offer_you(
			tr::now,
			lt_cost,
			AmountText(offer->amount),
			lt_name,
			GiftNameText(offer),
			tr::marked);
	}
	return tr::lng_action_gift_onchain_offer_incoming(
		tr::now,
		lt_amount,
		PrepareGramAmountWithFiat(
			&item->history()->session(),
			offer->amount,
			AmountText(offer->amount)),
		lt_name,
		GiftNameText(offer),
		tr::marked);
}

[[nodiscard]] TextWithEntities CompletedText(
		not_null<const HistoryServiceGiftOnchainOffer*> offer) {
	return (offer->buyer
		? tr::lng_action_gift_onchain_offer_bought
		: tr::lng_action_gift_onchain_offer_sold)(
			tr::now,
			lt_name,
			GiftNameText(offer),
			lt_amount,
			AmountText(offer->amount),
			tr::marked);
}

[[nodiscard]] TextWithEntities WithServedMoney(
		const QString &status,
		std::optional<int64> refund,
		std::optional<int64> fine) {
	auto result = tr::marked(status);
	if (refund) {
		result.append(QChar('\n')).append(
			tr::lng_action_gift_onchain_offer_refund(
				tr::now,
				lt_amount,
				AmountText(*refund),
				tr::marked));
	}
	if (fine) {
		result.append(QChar('\n')).append(
			tr::lng_action_gift_onchain_offer_fine(
				tr::now,
				lt_amount,
				AmountText(*fine),
				tr::marked));
	}
	return result;
}

[[nodiscard]] QString ResolutionText(
		not_null<const HistoryServiceGiftOnchainOffer*> offer,
		State state) {
	switch (state) {
	case State::Cancelled:
		return tr::lng_action_gift_onchain_offer_withdrawn(tr::now);
	case State::Declined:
		return offer->buyer
			? tr::lng_action_gift_onchain_offer_declined(tr::now)
			: tr::lng_action_gift_onchain_offer_declined_you(tr::now);
	case State::Expired:
		return tr::lng_action_gift_onchain_offer_expired(tr::now);
	case State::Open:
	case State::PastExpiry:
	case State::Cancelling:
	case State::Settled:
	case State::Accepted:
		return QString();
	}
	Unexpected("State in ResolutionText.");
}

[[nodiscard]] TextWithEntities StatusText(
		not_null<const HistoryServiceGiftOnchainOffer*> offer,
		TimeId now) {
	const auto buyer = offer->buyer;
	const auto state = ComputeGiftOnchainOfferState(offer, now);
	switch (state) {
	case State::Open:
		return tr::lng_action_gift_onchain_offer_expires(
			tr::now,
			lt_time,
			tr::bold(TimeLeftText(offer->expiresAt - now)),
			tr::marked);
	case State::PastExpiry:
		return tr::marked(buyer
			? tr::lng_action_gift_onchain_offer_expired_refunding(tr::now)
			: tr::lng_action_gift_onchain_offer_expired(tr::now));
	case State::Cancelling:
		return tr::lng_action_gift_onchain_offer_cancelling(
			tr::now,
			tr::marked);
	case State::Cancelled:
		return WithServedMoney(
			ResolutionText(offer, state),
			buyer ? offer->buyerRefund : std::nullopt,
			buyer ? offer->fine : std::nullopt);
	case State::Declined:
		return tr::marked(ResolutionText(offer, state));
	case State::Expired:
		return WithServedMoney(
			ResolutionText(offer, state),
			buyer ? offer->buyerRefund : std::nullopt,
			std::nullopt);
	case State::Settled:
		return tr::marked(buyer
			? tr::lng_action_gift_onchain_offer_buying(tr::now)
			: tr::lng_action_gift_onchain_offer_selling(tr::now));
	case State::Accepted:
		return CompletedText(offer);
	}
	Unexpected("State in GiftOnchainOfferCardText.");
}

} // namespace

void FillGiftOnchainOffer(
		not_null<HistoryItem*> item,
		const MTPDmessageActionStarGiftOnchainOffer &action) {
	Expects(item->Has<HistoryServiceGiftOnchainOffer>());

	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	const auto gift = Api::FromTL(
		&item->history()->session(),
		action.vgift());
	offer->gift = gift ? gift->unique : nullptr;
	offer->limitedCount = gift ? gift->limitedCount : 0;
	offer->limitedLeft = gift ? gift->limitedLeft : 0;
	offer->address = qs(action.voffer_address());
	offer->offerId = action.voffer_id().v;
	offer->amount = int64(action.vamount().v);
	offer->buyerRefund = ServedNanos(action.vbuyer_refund());
	offer->fine = ServedNanos(action.vfine());
	offer->expiresAt = action.vexpires_at().v;
	offer->feeBps = action.vfee_bps().v;
	offer->accepted = action.is_accepted();
	offer->declined = action.is_declined();
	offer->cancelled = action.is_cancelled();
	offer->expired = action.is_expired();
	offer->settled = action.is_settled();
	offer->buyer = action.is_buyer();
	offer->cancelling = action.is_cancelling();
}

GiftOnchainOfferState ComputeGiftOnchainOfferState(
		not_null<const HistoryServiceGiftOnchainOffer*> offer,
		TimeId now) {
	if (offer->accepted) {
		return State::Accepted;
	} else if (offer->declined) {
		return State::Declined;
	} else if (offer->cancelled) {
		return State::Cancelled;
	} else if (offer->expired) {
		return State::Expired;
	} else if (offer->settled) {
		return State::Settled;
	} else if (offer->cancelling) {
		return State::Cancelling;
	} else if (now >= offer->expiresAt) {
		return State::PastExpiry;
	}
	return State::Open;
}

bool GiftOnchainOfferReviewable(
		not_null<const HistoryServiceGiftOnchainOffer*> offer,
		TimeId now) {
	return offer->gift
		&& !offer->buyer
		&& (ComputeGiftOnchainOfferState(offer, now) == State::Open);
}

TimeId GiftOnchainOfferTimeLeft(
		not_null<const HistoryServiceGiftOnchainOffer*> offer,
		TimeId now) {
	const auto state = ComputeGiftOnchainOfferState(offer, now);
	return (state == State::Open) ? (offer->expiresAt - now) : TimeId();
}

bool CanWithdrawGiftOnchainOffer(
		not_null<const HistoryItem*> item,
		TimeId now) {
	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	return offer
		&& offer->gift
		&& offer->buyer
		&& item->isRegular()
		&& (ComputeGiftOnchainOfferState(offer, now) == State::Open);
}

PreparedServiceText PrepareGiftOnchainOfferText(
		not_null<const HistoryItem*> item) {
	auto result = PreparedServiceText();
	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	if (!offer || !offer->gift) {
		return result;
	}
	const auto state = ComputeGiftOnchainOfferState(
		offer,
		base::unixtime::now());
	if (state == State::Accepted) {
		result.text = CompletedText(offer);
		return result;
	}
	result.text = OfferText(item, offer);
	const auto resolution = ResolutionText(offer, state);
	if (!resolution.isEmpty()) {
		result.text.append(QChar(' ')).append(resolution);
	}
	return result;
}

TextWithEntities GiftOnchainOfferCardText(
		not_null<const HistoryItem*> item,
		TimeId now) {
	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	if (!offer || !offer->gift) {
		return tr::marked();
	}
	auto result = OfferText(item, offer);
	result.append(u"\n\n"_q).append(StatusText(offer, now));
	return result;
}

std::unique_ptr<Data::Media> CreateGiftOnchainOfferMedia(
		not_null<HistoryItem*> item) {
	const auto offer = item->Get<HistoryServiceGiftOnchainOffer>();
	if (!offer || !offer->gift) {
		return nullptr;
	}
	return std::make_unique<Data::MediaGiftBox>(
		item,
		item->from(),
		Data::GiftCode{
			.unique = offer->gift,
			.type = Data::GiftType::OnchainOffer,
		});
}
