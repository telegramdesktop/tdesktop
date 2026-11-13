/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session_onchain_gifts.h"

#include "wallet/wallet_address.h"
#include "wallet/wallet_session.h"

namespace Wallet {
namespace {

[[nodiscard]] std::optional<int64> ServedNano(
		tl::conditional<MTPlong> value) {
	return value ? std::make_optional(int64(value->v)) : std::nullopt;
}

void ReadOfferCounterparty(TransferItem &result, const QString &address) {
	if (address.isEmpty()) {
		return;
	}
	const auto parsed = ParseAddress(address);
	if (!parsed) {
		LOG(("Wallet Error: wallet.getTransactions sent an unusable "
			"offer counterparty address."));
		return;
	}
	result.counterparty = parsed->raw;
	result.counterpartyBounceable = parsed->friendly && parsed->bounceable;
}

template <typename Data>
void ReadOfferRefund(TransferItem &result, const Data &data) {
	ReadOfferCounterparty(
		result,
		qs(data.vcounterparty_address().value_or_empty()));
	result.onchainOffer.priceNano = ServedNano(data.vprice());
	result.onchainOffer.refundNano = int64(data.vrefund_amount().v);
}

void ReadSalePrice(
		TransferItem &result,
		const MTPDwalletOnchainGiftActionChangePrice &data) {
	result.onchainGift = TransferOnchainGift::List;
	const auto newPrice = int64(data.vnew_price().v);
	const auto oldPrice = ServedNano(data.vold_price());
	if (newPrice <= 0 || (oldPrice && *oldPrice < 0)) {
		LOG(("Wallet Error: wallet.getTransactions sent an unusable "
			"gift sale price."));
		return;
	}
	result.onchainOffer.priceNano = newPrice;
	if (oldPrice) {
		result.onchainGift = TransferOnchainGift::Reprice;
		result.onchainOffer.oldPriceNano = oldPrice;
	}
}

} // namespace

void ReadOnchainGiftAction(
		TransferItem &result,
		const MTPWalletOnchainGiftAction &action) {
	auto &offer = result.onchainOffer;
	switch (action.type()) {
	case mtpc_walletOnchainGiftActionBuy:
		result.onchainGift = TransferOnchainGift::Buy;
		break;
	case mtpc_walletOnchainGiftActionDelist:
		result.onchainGift = TransferOnchainGift::Delist;
		break;
	case mtpc_walletOnchainGiftActionChangePrice:
		ReadSalePrice(result, action.c_walletOnchainGiftActionChangePrice());
		break;
	case mtpc_walletOnchainGiftActionOfferCreate: {
		const auto &data = action.c_walletOnchainGiftActionOfferCreate();
		result.onchainGift = TransferOnchainGift::OfferCreate;
		ReadOfferCounterparty(
			result,
			qs(data.vcounterparty_address().value_or_empty()));
		offer.priceNano = int64(data.vprice().v);
		offer.fundingNano = int64(data.vamount().v);
		offer.serviceFeeNano = int64(data.vservice_fee().v);
		if (const auto expires = data.vexpires_at()) {
			offer.expiresAt = expires->v;
		}
	} break;
	case mtpc_walletOnchainGiftActionOfferAccept: {
		const auto &data = action.c_walletOnchainGiftActionOfferAccept();
		result.onchainGift = data.is_buyer()
			? TransferOnchainGift::OfferAcceptBuyer
			: TransferOnchainGift::OfferAcceptSeller;
		ReadOfferCounterparty(result, qs(data.vcounterparty_address()));
		offer.priceNano = int64(data.vprice().v);
		offer.serviceFeeNano = int64(data.vservice_fee().v);
	} break;
	case mtpc_walletOnchainGiftActionOfferCancel: {
		const auto &data = action.c_walletOnchainGiftActionOfferCancel();
		result.onchainGift = TransferOnchainGift::OfferCancel;
		ReadOfferRefund(result, data);
		offer.fineNano = ServedNano(data.vfine());
	} break;
	case mtpc_walletOnchainGiftActionOfferDecline:
		result.onchainGift = TransferOnchainGift::OfferDecline;
		ReadOfferRefund(
			result,
			action.c_walletOnchainGiftActionOfferDecline());
		break;
	case mtpc_walletOnchainGiftActionOfferExpire:
		result.onchainGift = TransferOnchainGift::OfferExpire;
		ReadOfferRefund(
			result,
			action.c_walletOnchainGiftActionOfferExpire());
		break;
	default:
		break;
	}
}

} // namespace Wallet
