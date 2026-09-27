/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_account.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <cmath>

namespace Gram {
namespace {

[[nodiscard]] std::optional<int64> ParseNonNegative(const QJsonValue &value) {
	if (value.isString()) {
		auto ok = false;
		const auto result = value.toString().toLongLong(&ok);
		return (ok && result >= 0) ? std::make_optional(result) : std::nullopt;
	} else if (value.isDouble()) {
		const auto result = value.toDouble();
		const auto exact = (result >= 0.)
			&& (result <= float64(int64(1) << 53))
			&& (result == std::floor(result));
		return exact ? std::make_optional(int64(result)) : std::nullopt;
	}
	return std::nullopt;
}

} // namespace

HttpRequest AddressInformationRequest(const QString &address) {
	return {
		.post = false,
		.endpoint = u"/api/v2/getAddressInformation"_q,
		.query = u"address="_q + ApiDetails::PercentEncoded(address),
	};
}

std::optional<AddressFunds> ParseAddressFunds(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto result = object.value(u"result"_q);
	if (!object.value(u"ok"_q).toBool() || !result.isObject()) {
		return std::nullopt;
	}
	const auto account = result.toObject();
	const auto balance = ParseNonNegative(account.value(u"balance"_q));
	if (!balance) {
		return std::nullopt;
	}
	const auto last = account.value(u"last_transaction_id"_q).toObject();
	const auto lt = ParseNonNegative(last.value(u"lt"_q));
	return AddressFunds{
		.balanceNano = *balance,
		.neverUsed = (lt == int64(0)),
	};
}

} // namespace Gram
