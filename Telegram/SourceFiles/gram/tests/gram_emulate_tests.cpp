/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_emulate.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <vector>

namespace Gram::Tests {
namespace {

const auto kAccount = u"0:1111111111111111111111111111111111111111111111111111111111111111"_q;
const auto kFees = u"11"_q;

[[nodiscard]] QJsonObject Transaction(
		const QString &account,
		const QString &totalFees,
		bool aborted,
		bool computeSuccess,
		bool actionSuccess,
		int exitCode,
		int resultCode) {
	return QJsonObject{
		{ u"account"_q, account },
		{ u"total_fees"_q, totalFees },
		{ u"description"_q, QJsonObject{
			{ u"type"_q, u"ord"_q },
			{ u"aborted"_q, aborted },
			{ u"compute_ph"_q, QJsonObject{
				{ u"success"_q, computeSuccess },
				{ u"exit_code"_q, exitCode },
			} },
			{ u"action"_q, QJsonObject{
				{ u"success"_q, actionSuccess },
				{ u"result_code"_q, resultCode },
			} },
		} },
	};
}

[[nodiscard]] QJsonObject Successful() {
	return Transaction(kAccount, kFees, false, true, true, 0, 0);
}

[[nodiscard]] QJsonObject WithoutDescriptionKey(
		QJsonObject root,
		const QString &key) {
	auto description = root.value(u"description"_q).toObject();
	description.remove(key);
	root.insert(u"description"_q, description);
	return root;
}

[[nodiscard]] QByteArray Body(
		const QJsonObject &root,
		const QString &rootHash = u"root"_q) {
	return QJsonDocument(QJsonObject{
		{ u"mc_block_seqno"_q, 17 },
		{ u"trace"_q, QJsonObject{ { u"tx_hash"_q, rootHash } } },
		{ u"transactions"_q, QJsonObject{ { u"root"_q, root } } },
		{ u"is_incomplete"_q, false },
	}).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray Body(
		const QString &account,
		const QString &totalFees,
		bool aborted,
		bool computeSuccess,
		bool actionSuccess,
		int exitCode,
		int resultCode) {
	return Body(Transaction(
		account,
		totalFees,
		aborted,
		computeSuccess,
		actionSuccess,
		exitCode,
		resultCode));
}

[[nodiscard]] QString CheckRejected(
		const QByteArray &body,
		const QString &what) {
	if (ParseEmulatedTrace(body)) {
		return u"expected nullopt for "_q
			+ what
			+ u": "_q
			+ QString::fromUtf8(body);
	}
	return QString();
}

} // namespace

std::vector<Check> EmulateChecks() {
	return {
		{ u"emulate_request_shape"_q, [] {
			const auto boc = u"AAAA"_q;
			const auto request = EmulateTraceRequest(boc);
			const auto expected = QJsonDocument(QJsonObject{
				{ u"boc"_q, boc },
				{ u"ignore_chksig"_q, true },
				{ u"include_code_data"_q, false },
				{ u"include_address_book"_q, false },
				{ u"include_metadata"_q, false },
				{ u"with_actions"_q, true },
				{ u"mc_block_seqno"_q, QJsonValue(QJsonValue::Null) },
			}).toJson(QJsonDocument::Compact);
			const auto failure = CheckRequest(
				request,
				true,
				u"/api/emulate/v1/emulateTrace"_q,
				QString(),
				expected);
			if (!failure.isEmpty()) {
				return failure;
			}
			const auto document = QJsonDocument::fromJson(request.payload);
			if (document.isNull() || !document.isObject()) {
				return u"payload: not a JSON object"_q;
			}
			const auto object = document.object();
			if (object.size() != 7) {
				return u"payload keys: got "_q
					+ QString::number(object.size())
					+ u", expected 7"_q;
			}
			struct Flag {
				QString key;
				bool value = false;
			};
			const auto flags = std::vector<Flag>{
				{ u"ignore_chksig"_q, true },
				{ u"include_code_data"_q, false },
				{ u"include_address_book"_q, false },
				{ u"include_metadata"_q, false },
				{ u"with_actions"_q, true },
			};
			for (const auto &flag : flags) {
				const auto value = object.value(flag.key);
				if (!value.isBool() || value.toBool() != flag.value) {
					return flag.key
						+ u": expected the bool "_q
						+ (flag.value ? u"true"_q : u"false"_q);
				}
			}
			const auto bocValue = object.value(u"boc"_q);
			if (!bocValue.isString() || bocValue.toString() != boc) {
				return u"boc: expected the input verbatim"_q;
			} else if (!object.value(u"mc_block_seqno"_q).isNull()) {
				return u"mc_block_seqno: expected null"_q;
			}
			return QString();
		} },
		{ u"emulate_parse_root_fee"_q, [] {
			const auto parsed = ParseEmulatedTrace(
				Body(kAccount, kFees, false, true, true, 0, 0));
			if (!parsed) {
				return u"parse failed for a successful root"_q;
			} else if (parsed->feeNano != 11) {
				return u"feeNano: got "_q
					+ QString::number(parsed->feeNano)
					+ u", expected 11"_q;
			} else if (parsed->account != kAccount) {
				return u"account: got "_q
					+ parsed->account
					+ u", expected "_q
					+ kAccount;
			}
			const auto zero = ParseEmulatedTrace(
				Body(kAccount, u"0"_q, false, true, true, 0, 0));
			if (!zero) {
				return u"parse failed for a zero fee"_q;
			} else if (zero->feeNano != 0) {
				return u"zero fee: got "_q
					+ QString::number(zero->feeNano)
					+ u", expected 0"_q;
			}
			return QString();
		} },
		{ u"emulate_parse_exit_code_one"_q, [] {
			const auto parsed = ParseEmulatedTrace(
				Body(kAccount, kFees, false, true, true, 1, 0));
			if (!parsed) {
				return u"exit code 1: expected the alternative TVM "
					u"success code to be accepted"_q;
			} else if (parsed->feeNano != 11) {
				return u"exit code 1: got feeNano "_q
					+ QString::number(parsed->feeNano)
					+ u", expected 11"_q;
			}
			return QString();
		} },
		{ u"emulate_parse_rejects_failed_phases"_q, [] {
			struct Case {
				bool computeSuccess = false;
				bool actionSuccess = false;
				int exitCode = 0;
				int resultCode = 0;
				QString what;
			};
			const auto cases = std::vector<Case>{
				{ true, true, 33, 0, u"compute exit code 33"_q },
				{ true, true, 0, 34, u"action result code 34"_q },
				{ true, true, 2, 0, u"compute exit code 2"_q },
				{ true, true, 0, 1, u"action result code 1"_q },
				{ false, true, 0, 0, u"compute success false"_q },
				{ true, false, 0, 0, u"action success false"_q },
			};
			for (const auto &entry : cases) {
				const auto failure = CheckRejected(
					Body(
						kAccount,
						kFees,
						false,
						entry.computeSuccess,
						entry.actionSuccess,
						entry.exitCode,
						entry.resultCode),
					entry.what);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"emulate_parse_rejects_missing_phase"_q, [] {
			const auto keys = std::vector<QString>{
				u"action"_q,
				u"compute_ph"_q,
			};
			for (const auto &key : keys) {
				const auto failure = CheckRejected(
					Body(WithoutDescriptionKey(Successful(), key)),
					u"a root without "_q + key);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			auto root = Successful();
			auto description = root.value(u"description"_q).toObject();
			description.insert(u"compute_ph"_q, QJsonObject{
				{ u"success"_q, true },
			});
			root.insert(u"description"_q, description);
			return CheckRejected(Body(root), u"a compute phase without a code"_q);
		} },
		{ u"emulate_parse_rejects_aborted"_q, [] {
			const auto aborted = CheckRejected(
				Body(kAccount, kFees, true, true, true, 0, 0),
				u"an aborted root"_q);
			if (!aborted.isEmpty()) {
				return aborted;
			}
			return CheckRejected(
				Body(WithoutDescriptionKey(Successful(), u"aborted"_q)),
				u"a root without the aborted flag"_q);
		} },
		{ u"emulate_parse_rejects_missing_root"_q, [] {
			const auto missing = CheckRejected(
				Body(Successful(), u"other"_q),
				u"a tx_hash naming no transaction"_q);
			if (!missing.isEmpty()) {
				return missing;
			}
			const auto empty = CheckRejected(
				Body(Successful(), QString()),
				u"an empty tx_hash"_q);
			if (!empty.isEmpty()) {
				return empty;
			}
			auto object = QJsonDocument::fromJson(Body(Successful())).object();
			object.remove(u"trace"_q);
			const auto noTrace = CheckRejected(
				QJsonDocument(object).toJson(QJsonDocument::Compact),
				u"a body without a trace"_q);
			if (!noTrace.isEmpty()) {
				return noTrace;
			}
			object = QJsonDocument::fromJson(Body(Successful())).object();
			object.remove(u"transactions"_q);
			return CheckRejected(
				QJsonDocument(object).toJson(QJsonDocument::Compact),
				u"a body without transactions"_q);
		} },
		{ u"emulate_parse_rejects_bad_fee"_q, [] {
			const auto fees = std::vector<QString>{
				u"x"_q,
				u"-1"_q,
				QString(),
			};
			for (const auto &fee : fees) {
				const auto failure = CheckRejected(
					Body(kAccount, fee, false, true, true, 0, 0),
					u"total_fees \""_q + fee + u"\""_q);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			auto root = Successful();
			root.insert(u"total_fees"_q, 11);
			return CheckRejected(Body(root), u"a numeric total_fees"_q);
		} },
		{ u"emulate_parse_rejects_error_body"_q, [] {
			const auto name = u"api-04-emulateTrace.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto recorded = CheckRejected(bytes, u"the error body"_q);
			if (!recorded.isEmpty()) {
				return recorded;
			}
			const auto bad = std::vector<QByteArray>{
				QByteArray("not json"),
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("{\"trace\":{\"tx_hash\":\"root\"}}"),
				QByteArray("{\"trace\":{\"tx_hash\":\"root\"},"
					"\"transactions\":{\"root\":{}}}"),
			};
			for (const auto &json : bad) {
				const auto failure = CheckRejected(json, u"a malformed body"_q);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
