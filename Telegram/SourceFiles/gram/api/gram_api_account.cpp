/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_account.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <cmath>

namespace Gram {
namespace {

[[nodiscard]] std::optional<QByteArray> ParsePublicKeyNumber(
		const QString &number) {
	auto digits = QStringView(number);
	const auto radix = digits.startsWith(u"0x") ? 16 : 10;
	if (radix == 16) {
		digits = digits.mid(2);
	}
	if (digits.isEmpty()) {
		return std::nullopt;
	}
	auto key = QByteArray(32, '\0');
	auto nonzero = false;
	for (const auto character : digits) {
		const auto code = character.unicode();
		const auto digit = (code >= '0' && code <= '9')
			? int(code - '0')
			: (radix == 16 && code >= 'a' && code <= 'f')
			? int(code - 'a') + 10
			: (radix == 16 && code >= 'A' && code <= 'F')
			? int(code - 'A') + 10
			: -1;
		if (digit < 0) {
			return std::nullopt;
		}
		nonzero = nonzero || (digit != 0);
		auto carry = digit;
		for (auto i = key.size(); i != 0;) {
			--i;
			const auto value = uchar(key[i]) * radix + carry;
			key[i] = char(value & 0xFF);
			carry = value >> 8;
		}
		if (carry) {
			return std::nullopt;
		}
	}
	return nonzero ? std::make_optional(key) : std::nullopt;
}

} // namespace

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

HttpRequest WalletPublicKeyRequest(const QString &address) {
	const auto body = QJsonObject{
		{ u"jsonrpc"_q, u"2.0"_q },
		{ u"id"_q, u"wallet-public-key"_q },
		{ u"method"_q, u"runGetMethod"_q },
		{ u"params"_q, QJsonObject{
			{ u"address"_q, address },
			{ u"method"_q, u"get_public_key"_q },
			{ u"stack"_q, QJsonArray() },
		} },
	};
	return {
		.post = true,
		.endpoint = u"/api/v2/jsonRPC"_q,
		.payload = QJsonDocument(body).toJson(QJsonDocument::Compact),
	};
}

std::optional<QByteArray> ParseWalletPublicKey(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto ok = object.value(u"ok"_q);
	const auto result = object.value(u"result"_q);
	if (object.contains(u"error"_q)
		|| (!ok.isUndefined() && (!ok.isBool() || !ok.toBool()))
		|| !result.isObject()) {
		return std::nullopt;
	}
	const auto data = result.toObject();
	const auto exitCode = data.value(u"exit_code"_q);
	const auto stack = data.value(u"stack"_q).toArray();
	if (!exitCode.isDouble()
		|| exitCode.toDouble() != 0.
		|| stack.size() != 1) {
		return std::nullopt;
	}
	const auto first = stack[0];
	auto number = QJsonValue();
	if (first.isArray()) {
		const auto entry = first.toArray();
		if (entry.size() == 2 && entry[0].toString() == u"num"_q) {
			number = entry[1];
		}
	} else if (first.isObject()) {
		const auto entry = first.toObject();
		if (entry.value(u"type"_q).toString() == u"num"_q) {
			number = entry.value(u"value"_q);
		}
	}
	return number.isString()
		? ParsePublicKeyNumber(number.toString())
		: std::nullopt;
}

} // namespace Gram
