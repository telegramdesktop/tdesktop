/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_emulate.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace Gram {
namespace {

const auto kEmulateTraceEndpoint = u"/api/emulate/v1/emulateTrace"_q;

[[nodiscard]] bool PhaseSucceeded(
		const QJsonObject &description,
		const QString &phaseKey,
		const QString &codeKey,
		Fn<bool(int)> codeAllowed) {
	const auto phase = description.value(phaseKey);
	if (!phase.isObject()) {
		return false;
	}
	const auto object = phase.toObject();
	const auto success = object.value(u"success"_q);
	const auto code = object.value(codeKey);
	return success.isBool()
		&& success.toBool()
		&& code.isDouble()
		&& codeAllowed(code.toInt(-1));
}

} // namespace

HttpRequest EmulateTraceRequest(const QString &bocBase64) {
	return {
		.post = true,
		.endpoint = kEmulateTraceEndpoint,
		.payload = QJsonDocument(QJsonObject{
			{ u"boc"_q, bocBase64 },
			{ u"ignore_chksig"_q, true },
			{ u"include_code_data"_q, false },
			{ u"include_address_book"_q, false },
			{ u"include_metadata"_q, false },
			{ u"with_actions"_q, true },
			{ u"mc_block_seqno"_q, QJsonValue(QJsonValue::Null) },
		}).toJson(QJsonDocument::Compact),
	};
}

std::optional<EmulatedTrace> ParseEmulatedTrace(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto trace = object.value(u"trace"_q);
	if (!trace.isObject()) {
		return std::nullopt;
	}
	const auto hash = trace.toObject().value(u"tx_hash"_q);
	if (!hash.isString() || hash.toString().isEmpty()) {
		return std::nullopt;
	}
	const auto transactions = object.value(u"transactions"_q);
	if (!transactions.isObject()) {
		return std::nullopt;
	}
	const auto root = transactions.toObject().value(hash.toString());
	if (!root.isObject()) {
		return std::nullopt;
	}
	const auto transaction = root.toObject();
	const auto account = transaction.value(u"account"_q);
	const auto fees = transaction.value(u"total_fees"_q);
	if (!account.isString()
		|| account.toString().isEmpty()
		|| !fees.isString()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto feeNano = fees.toString().toLongLong(&ok);
	if (!ok || feeNano < 0) {
		return std::nullopt;
	}
	const auto description = transaction.value(u"description"_q);
	if (!description.isObject()) {
		return std::nullopt;
	}
	const auto phases = description.toObject();
	const auto aborted = phases.value(u"aborted"_q);
	if (!aborted.isBool() || aborted.toBool()) {
		return std::nullopt;
	}
	const auto compute = PhaseSucceeded(
		phases,
		u"compute_ph"_q,
		u"exit_code"_q,
		[](int code) { return (code == 0) || (code == 1); });
	const auto action = PhaseSucceeded(
		phases,
		u"action"_q,
		u"result_code"_q,
		[](int code) { return (code == 0); });
	if (!compute || !action) {
		return std::nullopt;
	}
	return EmulatedTrace{
		.feeNano = feeNano,
		.account = account.toString(),
	};
}

} // namespace Gram
