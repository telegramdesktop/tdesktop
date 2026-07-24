/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_send.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace Gram {
namespace {

[[nodiscard]] QString DescribeFailure(const QJsonObject &description) {
	const auto compute = description.value(u"compute_ph"_q).toObject();
	if (!compute.value(u"success"_q).toBool()) {
		return u"exit_code "_q
			+ QString::number(compute.value(u"exit_code"_q).toInt());
	}
	const auto action = description.value(u"action"_q).toObject();
	if (!action.value(u"success"_q).toBool()) {
		return u"action result_code "_q
			+ QString::number(action.value(u"result_code"_q).toInt());
	}
	return u"aborted"_q;
}

} // namespace

HttpRequest SendMessageRequest(const QByteArray &bocBase64) {
	auto result = HttpRequest();
	result.post = true;
	result.endpoint = u"/api/v3/message"_q;
	result.payload = QJsonDocument(QJsonObject{
		{ u"boc"_q, QString::fromLatin1(bocBase64) },
	}).toJson(QJsonDocument::Compact);
	return result;
}

std::optional<SentInfo> ParseSendResult(const QByteArray &json) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto hash = object.value(u"message_hash"_q);
	const auto hashNorm = object.value(u"message_hash_norm"_q);
	if (!hash.isString() || !hashNorm.isString()) {
		return std::nullopt;
	}
	auto result = SentInfo();
	result.messageHash = ApiDetails::DecodeAnyBase64(hash.toString());
	result.messageHashNorm = ApiDetails::DecodeAnyBase64(hashNorm.toString());
	return result;
}

HttpRequest EmulateTraceRequest(const QByteArray &bocBase64) {
	auto result = HttpRequest();
	result.post = true;
	result.endpoint = u"/api/emulate/v1/emulateTrace"_q;
	result.payload = QJsonDocument(QJsonObject{
		{ u"boc"_q, QString::fromLatin1(bocBase64) },
		{ u"ignore_chksig"_q, true },
		{ u"include_address_book"_q, false },
		{ u"include_code_data"_q, false },
		{ u"include_metadata"_q, false },
		{ u"with_actions"_q, true },
	}).toJson(QJsonDocument::Compact);
	return result;
}

std::optional<EmulationResult> ParseEmulateTrace(const QByteArray &json) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto transactionsValue = object.value(u"transactions"_q);
	if (!transactionsValue.isObject()) {
		return std::nullopt;
	}
	const auto transactions = transactionsValue.toObject();

	auto result = EmulationResult();
	result.success = true;
	for (const auto &value : transactions) {
		if (!value.isObject()) {
			return std::nullopt;
		}
		const auto tx = value.toObject();
		const auto fees = ApiDetails::ParseInt64String(
			tx.value(u"total_fees"_q));
		if (!fees) {
			return std::nullopt;
		}
		result.totalFeeNano += *fees;

		const auto description = tx.value(u"description"_q).toObject();
		if (!ApiDetails::ComputeSuccess(description) && result.success) {
			result.success = false;
			result.error = DescribeFailure(description);
		}
	}

	const auto trace = object.value(u"trace"_q).toObject();
	const auto rootHash = trace.value(u"tx_hash"_q).toString();
	auto root = QJsonObject();
	if (!rootHash.isEmpty() && transactions.contains(rootHash)) {
		root = transactions.value(rootHash).toObject();
	} else if (!transactions.isEmpty()) {
		root = transactions.begin().value().toObject();
	}
	const auto outMsgs = root.value(u"out_msgs"_q).toArray();
	if (!outMsgs.isEmpty()) {
		const auto first = outMsgs.at(0).toObject();
		if (const auto sent = ApiDetails::ParseInt64String(
				first.value(u"value"_q))) {
			result.sentNano = *sent;
		}
	}

	return result;
}

} // namespace Gram
