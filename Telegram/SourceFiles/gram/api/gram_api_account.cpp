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

namespace Gram {
namespace {

[[nodiscard]] std::optional<AccountStatus> ParseStatus(const QString &value) {
	if (value == u"active"_q) {
		return AccountStatus::Active;
	} else if (value == u"frozen"_q) {
		return AccountStatus::Frozen;
	} else if (value == u"uninit"_q || value == u"uninitialized"_q) {
		return AccountStatus::Uninit;
	} else if (value == u"nonexist"_q || value == u"non-existing"_q) {
		return AccountStatus::NonExisting;
	}
	return std::nullopt;
}

} // namespace

HttpRequest AddressInformationRequest(const QString &address) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/addressInformation"_q;
	result.query = u"address="_q
		+ ApiDetails::PercentEncoded(address)
		+ u"&include_boc=true"_q;
	return result;
}

std::optional<AccountState> ParseAccountState(const QByteArray &json) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();

	const auto balance = ApiDetails::ParseInt64String(
		object.value(u"balance"_q));
	if (!balance) {
		return std::nullopt;
	}
	const auto statusValue = object.value(u"status"_q);
	if (!statusValue.isString()) {
		return std::nullopt;
	}
	const auto status = ParseStatus(statusValue.toString());
	if (!status) {
		return std::nullopt;
	}

	auto result = AccountState();
	result.balanceNano = *balance;
	result.status = *status;

	const auto data = object.value(u"data"_q);
	if (data.isString()) {
		result.dataBoc = QByteArray::fromBase64(data.toString().toLatin1());
	} else if (!data.isNull() && !data.isUndefined()) {
		return std::nullopt;
	}

	const auto lastTxHash = object.value(u"last_transaction_hash"_q);
	if (lastTxHash.isString()) {
		result.lastTxHash = QByteArray::fromBase64(
			lastTxHash.toString().toLatin1());
	} else if (!lastTxHash.isNull() && !lastTxHash.isUndefined()) {
		return std::nullopt;
	}

	const auto lastTxLt = object.value(u"last_transaction_lt"_q);
	if (!lastTxLt.isNull() && !lastTxLt.isUndefined()) {
		const auto parsed = ApiDetails::ParseUint64String(lastTxLt);
		if (!parsed) {
			return std::nullopt;
		}
		result.lastTxLt = *parsed;
	}

	return result;
}

} // namespace Gram
