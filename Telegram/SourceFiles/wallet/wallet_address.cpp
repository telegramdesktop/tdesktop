/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_address.h"

#include "base/unixtime.h"

#include "wallet_engine.hpp"

namespace Wallet {
namespace {

namespace engine = wallet_engine;

} // namespace

std::optional<ParsedAddress> ParseAddress(const QString &text) {
	try {
		const auto info = engine::parse_ton_address(text.toStdString());
		auto result = ParsedAddress();
		result.raw = QString::fromStdString(info.raw);
		const auto &format = info.format.get_variant();
		const auto friendly = std::get_if<
			engine::TonAddressFormat::kUserFriendly>(&format);
		if (friendly) {
			result.friendly = true;
			result.bounceable = friendly->bounceable;
			result.testnet = friendly->testnet;
		}
		return result;
	} catch (...) {
		return std::nullopt;
	}
}

QString CanonicalAddress(const QString &text) {
	try {
		return QString::fromStdString(engine::convert_ton_address(
			text.toStdString(),
			engine::TonAddressFormat(engine::TonAddressFormat::kRaw{})));
	} catch (...) {
		return QString();
	}
}

QString FormatFriendly(
		const QString &raw,
		bool bounceable,
		bool testnet) {
	try {
		return QString::fromStdString(engine::convert_ton_address(
			raw.toStdString(),
			engine::TonAddressFormat(
				engine::TonAddressFormat::kUserFriendly{
					.bounceable = bounceable,
					.testnet = testnet,
				})));
	} catch (...) {
		LOG(("Wallet Error: cannot format address."));
		return QString();
	}
}

bool TransferLinkExpired(std::optional<uint64> expiresAt) {
	const auto now = base::unixtime::now();
	return expiresAt && (now > 0) && (uint64(now) >= *expiresAt);
}

std::optional<TransferLink> ParseTransferLink(const QString &url) {
	try {
		const auto link = engine::parse_ton_transfer_link(
			url.toStdString());
		const auto &asset = link.asset.get_variant();
		if (std::get_if<engine::TonTransferAsset::kJetton>(&asset)) {
			return std::nullopt;
		}
		auto result = TransferLink();
		result.address = QString::fromStdString(link.recipient);
		if (link.amount) {
			auto ok = false;
			const auto amount = QString::fromStdString(
				*link.amount).toLongLong(&ok);
			if (!ok) {
				return std::nullopt;
			}
			result.amountNano = amount;
		}
		const auto &payload = link.payload.get_variant();
		if (std::get_if<engine::TonTransferPayload::kBoc>(&payload)) {
			return std::nullopt;
		}
		const auto comment = std::get_if<
			engine::TonTransferPayload::kText>(&payload);
		if (comment) {
			result.comment = QString::fromStdString(comment->text);
		}
		const auto &expiration = link.expiration.get_variant();
		const auto exact = std::get_if<
			engine::SendExpiration::kExact>(&expiration);
		if (exact) {
			result.expiresAt = exact->unix_timestamp;
		}
		return result;
	} catch (...) {
		return std::nullopt;
	}
}

} // namespace Wallet
