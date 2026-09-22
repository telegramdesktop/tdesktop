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

HttpRequest AddressInformationRequest(const QString &address) {
	return {
		.post = false,
		.endpoint = u"/api/v2/getAddressInformation"_q,
		.query = u"address="_q + ApiDetails::PercentEncoded(address),
	};
}

std::optional<int64> ParseAddressBalance(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto result = object.value(u"result"_q);
	if (!object.value(u"ok"_q).toBool() || !result.isObject()) {
		return std::nullopt;
	}
	const auto balance = result.toObject().value(u"balance"_q);
	if (balance.isString()) {
		auto ok = false;
		const auto value = balance.toString().toLongLong(&ok);
		return (ok && value >= 0) ? std::make_optional(value) : std::nullopt;
	} else if (balance.isDouble()) {
		const auto value = balance.toDouble();
		const auto exact = (value >= 0.)
			&& (value <= float64(int64(1) << 53))
			&& (value == std::floor(value));
		return exact ? std::make_optional(int64(value)) : std::nullopt;
	}
	return std::nullopt;
}

} // namespace Gram
