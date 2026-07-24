/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_history.h"
#include "gram/api/gram_api_request.h"
#include "gram/api/gram_api_send.h"
#include "gram/ton/gram_address.h"

#include <QtCore/QFile>

#include <vector>

namespace Gram::Tests {
namespace {

const auto kAcc1Raw = u"0:9DA971AF38D2F03ABDF308D5F91636A97E5A2B07A66C39D71D7CBAE3B032EDDC"_q;
const auto kAcc2Raw = u"0:BC1B748F5D26B74D857798FF4DD4252A2B79CF51B232AE41BE1F19E8CD9547B7"_q;
const auto kAcc1Enc = u"0%3A9DA971AF38D2F03ABDF308D5F91636A97E5A2B07A66C39D71D7CBAE3B032EDDC"_q;
const auto kAcc2Friendly = u"UQC8G3SPXSa3TYV3mP9N1CUqK3nPUbIyrkG-HxnozZVHt2Iv"_q;

[[nodiscard]] QByteArray ReadFixture(const QString &name) {
	auto file = QFile(
		QString::fromUtf8(GRAM_TEST_FIXTURES_PATH) + u"/"_q + name);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll().trimmed();
}

[[nodiscard]] Address Acc1() {
	const auto parsed = ParseAddress(kAcc1Raw);
	return parsed ? parsed->address : Address();
}

[[nodiscard]] Address Acc2() {
	const auto parsed = ParseAddress(kAcc2Raw);
	return parsed ? parsed->address : Address();
}

[[nodiscard]] Address Raw(const QString &text) {
	const auto parsed = ParseAddress(text);
	return parsed ? parsed->address : Address();
}

[[nodiscard]] QString CheckRequest(
		const HttpRequest &got,
		bool post,
		const QString &endpoint,
		const QString &query,
		const QByteArray &payload) {
	if (got.post != post) {
		return u"post: got "_q
			+ (got.post ? u"true"_q : u"false"_q)
			+ u", expected "_q
			+ (post ? u"true"_q : u"false"_q);
	} else if (got.endpoint != endpoint) {
		return u"endpoint: got "_q
			+ got.endpoint
			+ u", expected "_q
			+ endpoint;
	} else if (got.query != query) {
		return u"query: got "_q + got.query + u", expected "_q + query;
	} else if (got.payload != payload) {
		return u"payload: got "_q
			+ QString::fromUtf8(got.payload)
			+ u", expected "_q
			+ QString::fromUtf8(payload);
	}
	return QString();
}

[[nodiscard]] QString CheckTracesItem(
		const TransferItem &got,
		const TransferItem &expected) {
	if (got.kind != expected.kind) {
		return u"kind: got "_q
			+ QString::number(int(got.kind))
			+ u", expected "_q
			+ QString::number(int(expected.kind));
	} else if (got.incoming != expected.incoming) {
		return u"incoming: got "_q
			+ (got.incoming ? u"true"_q : u"false"_q)
			+ u", expected "_q
			+ (expected.incoming ? u"true"_q : u"false"_q);
	} else if (got.counterparty != expected.counterparty) {
		return u"counterparty: got "_q
			+ FormatRaw(got.counterparty)
			+ u", expected "_q
			+ FormatRaw(expected.counterparty);
	} else if (got.amountNano != expected.amountNano) {
		return u"amount: got "_q
			+ QString::number(got.amountNano)
			+ u", expected "_q
			+ QString::number(expected.amountNano);
	} else if (got.feeNano != expected.feeNano) {
		return u"fee: got "_q
			+ QString::number(got.feeNano)
			+ u", expected "_q
			+ QString::number(expected.feeNano);
	} else if (got.comment != expected.comment) {
		return u"comment: got \""_q
			+ got.comment
			+ u"\", expected \""_q
			+ expected.comment
			+ u"\""_q;
	} else if (got.date != expected.date) {
		return u"date: got "_q
			+ QString::number(got.date)
			+ u", expected "_q
			+ QString::number(expected.date);
	} else if (got.lt != expected.lt) {
		return u"lt: got "_q
			+ QString::number(got.lt)
			+ u", expected "_q
			+ QString::number(expected.lt);
	} else if (got.status != expected.status) {
		return u"status: got "_q
			+ QString::number(int(got.status))
			+ u", expected "_q
			+ QString::number(int(expected.status));
	}
	return QString();
}

} // namespace

std::vector<Check> ApiChecks() {
	return {
		{ u"api_builder_address_information"_q, [] {
			const auto raw = CheckRequest(
				AddressInformationRequest(kAcc1Raw),
				false,
				u"/api/v3/addressInformation"_q,
				u"address="_q + kAcc1Enc + u"&include_boc=true"_q,
				QByteArray());
			if (!raw.isEmpty()) {
				return u"raw: "_q + raw;
			}
			const auto friendly = CheckRequest(
				AddressInformationRequest(kAcc2Friendly),
				false,
				u"/api/v3/addressInformation"_q,
				u"address="_q + kAcc2Friendly + u"&include_boc=true"_q,
				QByteArray());
			return friendly.isEmpty()
				? QString()
				: (u"friendly: "_q + friendly);
		} },
		{ u"api_builder_traces_and_transactions"_q, [] {
			const auto traces = CheckRequest(
				TracesRequest(kAcc1Raw, 20, 0),
				false,
				u"/api/v3/traces"_q,
				u"account="_q + kAcc1Enc + u"&limit=20&offset=0"_q,
				QByteArray());
			if (!traces.isEmpty()) {
				return u"traces: "_q + traces;
			}
			const auto clamped = CheckRequest(
				TracesRequest(kAcc1Raw, 150, -3),
				false,
				u"/api/v3/traces"_q,
				u"account="_q + kAcc1Enc + u"&limit=100&offset=0"_q,
				QByteArray());
			if (!clamped.isEmpty()) {
				return u"clamped: "_q + clamped;
			}
			const auto transactions = CheckRequest(
				TransactionsRequest(kAcc1Raw, 20, 0),
				false,
				u"/api/v3/transactions"_q,
				u"account="_q + kAcc1Enc + u"&limit=20&offset=0"_q,
				QByteArray());
			return transactions.isEmpty()
				? QString()
				: (u"transactions: "_q + transactions);
		} },
		{ u"api_builder_transactions_by_message"_q, [] {
			const auto msgHash = QByteArray::fromBase64(
				"+CfWeu7ILG3oO19QghbBv0dja9W+vAZ+Bq8t4Rq5nAo=");
			return CheckRequest(
				TransactionsByMessageRequest(msgHash),
				false,
				u"/api/v3/transactionsByMessage"_q,
				u"msg_hash=%2BCfWeu7ILG3oO19QghbBv0dja9W%2BvAZ%2BBq8t4Rq5nAo%3D"_q,
				QByteArray());
		} },
		{ u"api_builder_send_message"_q, [] {
			return CheckRequest(
				SendMessageRequest("dGVzdA=="),
				true,
				u"/api/v3/message"_q,
				QString(),
				QByteArray("{\"boc\":\"dGVzdA==\"}"));
		} },
		{ u"api_builder_emulate_trace"_q, [] {
			return CheckRequest(
				EmulateTraceRequest("dGVzdA=="),
				true,
				u"/api/emulate/v1/emulateTrace"_q,
				QString(),
				QByteArray("{\"boc\":\"dGVzdA==\","
					"\"ignore_chksig\":true,"
					"\"include_address_book\":false,"
					"\"include_code_data\":false,"
					"\"include_metadata\":false,"
					"\"with_actions\":true}"));
		} },
		{ u"api_error_fixture_bodies"_q, [] {
			struct Case {
				QString name;
				QString message;
			};
			const auto cases = std::vector<Case>{
				{ u"api-02-traces.json"_q,
					u"timeout: context deadline exceeded"_q },
				{ u"api-04-emulateTrace.json"_q,
					u"internal server error: Cannot POST "
					u"/api/emulate/v1/emulateTrace"_q },
				{ u"api-05-badAddress.json"_q,
					u"failed to decode: schema: "
					u"error converting value for \"address\""_q },
				{ u"api-06-unknownEndpoint.json"_q,
					u"internal server error: "
					u"Cannot GET definitelyNotAnEndpoint"_q },
				{ u"api-07-badPostBody.json"_q,
					u"invalid character 'n' looking "
					u"for beginning of object key string"_q },
				{ u"api-09-tracesAlt.json"_q,
					u"timeout: context deadline exceeded"_q },
				{ u"api-10-emulateV3.json"_q,
					u"internal server error: Method Not Allowed"_q },
				{ u"api-13-emulateV3Get.json"_q,
					u"internal server error: Cannot GET emulateTrace"_q },
				{ u"api-14-msgHashRaw.json"_q,
					u"failed to decode: schema: error converting "
					u"value for index 0 of \"msg_hash\""_q },
			};
			for (const auto &entry : cases) {
				const auto bytes = ReadFixture(entry.name);
				if (bytes.isEmpty()) {
					return u"fixture read failed: "_q + entry.name;
				}
				const auto error = ParseApiError(bytes);
				if (!error) {
					return entry.name + u": expected an error"_q;
				} else if (error->message != entry.message) {
					return entry.name
						+ u": got \""_q
						+ error->message
						+ u"\""_q;
				} else if (error->code != 0) {
					return entry.name + u": expected code 0"_q;
				}
			}
			const auto traces = ReadFixture(u"api-02-traces.json"_q);
			if (traces.isEmpty()) {
				return u"fixture read failed: api-02-traces.json"_q;
			} else if (ParseTraces(traces, Acc1(), 20)) {
				return u"api-02: ParseTraces expected nullopt"_q;
			}
			const auto emulate = ReadFixture(u"api-04-emulateTrace.json"_q);
			if (emulate.isEmpty()) {
				return u"fixture read failed: api-04-emulateTrace.json"_q;
			} else if (ParseEmulateTrace(emulate)) {
				return u"api-04: ParseEmulateTrace expected nullopt"_q;
			}
			const auto byMessage = ReadFixture(u"api-14-msgHashRaw.json"_q);
			if (byMessage.isEmpty()) {
				return u"fixture read failed: api-14-msgHashRaw.json"_q;
			} else if (ParseTransactionsByMessageFound(byMessage)) {
				return u"api-14: ParseTransactionsByMessageFound "
					u"expected nullopt"_q;
			}
			return QString();
		} },
		{ u"api_error_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("{\"error\":42}"),
				QByteArray("{\"error\":null}"),
			};
			for (const auto &json : bad) {
				if (ParseApiError(json)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			if (ParseApiError(QByteArray("{\"balance\":\"1\"}"))) {
				return u"expected nullopt for a non-error object"_q;
			}
			return QString();
		} },
		{ u"api_account_state_fixture"_q, [] {
			const auto name = u"api-01-addressInformation.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto state = ParseAccountState(bytes);
			if (!state) {
				return u"parse failed"_q;
			} else if (state->balanceNano != 1592537944127325LL) {
				return u"balance: got "_q
					+ QString::number(state->balanceNano);
			} else if (state->status != AccountStatus::Active) {
				return u"status: expected Active"_q;
			}
			const auto expectedData = QByteArray::fromBase64(
				"te6ccgEBAQEAKgAAUAAAAVcpqaMXcsnta2Km4uu"
				"hSpO5BGLno2d3e+uKOPsVufM4RNIs4v8=");
			if (state->dataBoc != expectedData) {
				return u"dataBoc mismatch"_q;
			} else if (state->lastTxLt != 89815382000012ULL) {
				return u"lastTxLt: got "_q
					+ QString::number(state->lastTxLt);
			}
			const auto expectedHash = QByteArray::fromBase64(
				"ZZCcUbZxDdzd+bfSMWZtOfGkwgNq4CKuYY0MP+/GBFM=");
			if (state->lastTxHash != expectedHash) {
				return u"lastTxHash mismatch"_q;
			} else if (state->lastTxHash.size() != 32) {
				return u"lastTxHash size: got "_q
					+ QString::number(state->lastTxHash.size());
			}
			return QString();
		} },
		{ u"api_account_state_statuses"_q, [] {
			struct Case {
				QByteArray status;
				AccountStatus expected;
			};
			const auto cases = std::vector<Case>{
				{ "active", AccountStatus::Active },
				{ "frozen", AccountStatus::Frozen },
				{ "uninit", AccountStatus::Uninit },
				{ "nonexist", AccountStatus::NonExisting },
				{ "uninitialized", AccountStatus::Uninit },
				{ "non-existing", AccountStatus::NonExisting },
			};
			for (const auto &entry : cases) {
				const auto label = QString::fromUtf8(entry.status);
				const auto json = QByteArray("{\"balance\":\"0\",\"status\":\"")
					+ entry.status
					+ "\"}";
				const auto state = ParseAccountState(json);
				if (!state) {
					return u"parse failed for "_q + label;
				} else if (state->status != entry.expected) {
					return u"status mismatch for "_q + label;
				} else if (state->balanceNano != 0) {
					return u"balance: expected 0 for "_q + label;
				} else if (!state->dataBoc.isEmpty()) {
					return u"dataBoc: expected empty for "_q + label;
				} else if (!state->lastTxHash.isEmpty()
					|| state->lastTxLt != 0) {
					return u"last tx: expected defaults for "_q + label;
				}
			}
			if (ParseAccountState(
					QByteArray("{\"balance\":\"0\",\"status\":\"bogus\"}"))) {
				return u"unknown status: expected nullopt"_q;
			}
			const auto nullData = ParseAccountState(QByteArray(
				"{\"balance\":\"5\",\"status\":\"active\",\"data\":null}"));
			if (!nullData) {
				return u"null data: parse failed"_q;
			} else if (!nullData->dataBoc.isEmpty()) {
				return u"null data: expected empty dataBoc"_q;
			}
			return QString();
		} },
		{ u"api_account_state_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("{\"balance\":123,\"status\":\"active\"}"),
				QByteArray("{\"balance\":true,\"status\":\"active\"}"),
				QByteArray("{\"balance\":\"999999999999999999999999\","
					"\"status\":\"active\"}"),
				QByteArray("{\"status\":\"active\"}"),
			};
			for (const auto &json : bad) {
				if (ParseAccountState(json)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			const auto name = u"api-03-runGetMethod.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			} else if (ParseAccountState(bytes)) {
				return u"api-03: ParseAccountState expected nullopt"_q;
			} else if (ParseTraces(bytes, Acc1(), 20)) {
				return u"api-03: ParseTraces expected nullopt"_q;
			} else if (ParseSendResult(bytes)) {
				return u"api-03: ParseSendResult expected nullopt"_q;
			}
			return QString();
		} },
		{ u"api_transactions_by_message_found"_q, [] {
			const auto name = u"api-15-msgHashEncoded.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto found = ParseTransactionsByMessageFound(bytes);
			if (!found) {
				return u"api-15: expected a value"_q;
			} else if (*found) {
				return u"api-15: expected false (empty)"_q;
			}
			const auto nonEmpty = ParseTransactionsByMessageFound(
				QByteArray("{\"transactions\":[{}]}"));
			if (!nonEmpty) {
				return u"non-empty: expected a value"_q;
			} else if (!*nonEmpty) {
				return u"non-empty: expected true"_q;
			}
			if (ParseTransactionsByMessageFound(QByteArray("{}"))) {
				return u"empty object: expected nullopt"_q;
			} else if (ParseTransactionsByMessageFound(QByteArray("[]"))) {
				return u"array: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"api_send_result_fixture"_q, [] {
			const auto name = u"api-send-result.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto result = ParseSendResult(bytes);
			if (!result) {
				return u"parse failed"_q;
			}
			const auto expectedNorm = QByteArray::fromHex(
				ReadFixture(u"normalized-ext-hash.hex"_q));
			if (expectedNorm.isEmpty()) {
				return u"fixture read failed: normalized-ext-hash.hex"_q;
			} else if (result->messageHashNorm != expectedNorm) {
				return u"messageHashNorm mismatch"_q;
			}
			const auto expectedHash = QByteArray::fromBase64(
				"AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=");
			if (result->messageHash != expectedHash) {
				return u"messageHash mismatch"_q;
			}
			if (ParseSendResult(QByteArray("{\"message_hash\":\"AA==\"}"))) {
				return u"missing norm: expected nullopt"_q;
			} else if (ParseSendResult(QByteArray("{}"))) {
				return u"empty: expected nullopt"_q;
			} else if (ParseSendResult(QByteArray(
				"{\"message_hash\":1,\"message_hash_norm\":\"AA==\"}"))) {
				return u"wrong type: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"api_emulate_fixture"_q, [] {
			const auto name = u"api-emulation.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto result = ParseEmulateTrace(bytes);
			if (!result) {
				return u"parse failed"_q;
			} else if (!result->success) {
				return u"expected success"_q;
			} else if (result->totalFeeNano != 1000000) {
				return u"totalFee: got "_q
					+ QString::number(result->totalFeeNano);
			} else if (result->sentNano != 1000000000) {
				return u"sent: got "_q + QString::number(result->sentNano);
			} else if (!result->error.isEmpty()) {
				return u"expected empty error, got "_q + result->error;
			}
			return QString();
		} },
		{ u"api_emulate_failed_fixture"_q, [] {
			const auto name = u"api-emulation-failed.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto result = ParseEmulateTrace(bytes);
			if (!result) {
				return u"parse failed"_q;
			} else if (result->success) {
				return u"expected failure"_q;
			} else if (result->error != u"exit_code 34"_q) {
				return u"error: got \""_q + result->error + u"\""_q;
			} else if (result->totalFeeNano != 1000000) {
				return u"totalFee: got "_q
					+ QString::number(result->totalFeeNano);
			}
			return QString();
		} },
		{ u"api_emulate_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("{\"transactions\":[]}"),
				QByteArray("{\"transactions\":{\"a\":{\"total_fees\":123}}}"),
				QByteArray("{\"transactions\":{\"a\":"
					"{\"total_fees\":\"99999999999999999999\"}}}"),
			};
			for (const auto &json : bad) {
				if (ParseEmulateTrace(json)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			return QString();
		} },
		{ u"api_traces_outgoing_fixture"_q, [] {
			const auto name = u"api-ton-sent-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::Transfer;
			expected.incoming = false;
			expected.counterparty = Raw(u"0:4E3664FAEE814FBDB6C1CB36D72BAC52"
				u"D15E186C11ECD664FE1B3F1A7CFD801D"_q);
			expected.amountNano = 1000000000;
			expected.feeNano = 2062939;
			expected.date = 1754154889;
			expected.lt = 60054975000001ULL;
			expected.status = TransferItem::Status::Success;
			const auto &item = page->list.front();
			const auto failure = CheckTracesItem(item, expected);
			if (!failure.isEmpty()) {
				return failure;
			}
			const auto traceId = QByteArray::fromBase64(
				"9QeaIiXlgf8UD9boljxboc15XqfaJ2EWXLfH94apqEc=");
			if (item.traceId != traceId) {
				return u"traceId mismatch"_q;
			}
			const auto extHashNorm = QByteArray::fromBase64(
				"JlAMiTCOjbxXFbDHl/5va9yqq0ffnn8As5NAFZDeyMg=");
			if (item.externalHashNorm != extHashNorm) {
				return u"externalHashNorm mismatch"_q;
			} else if (page->hasNext) {
				return u"hasNext: expected false"_q;
			}
			const auto limited = ParseTraces(bytes, Acc1(), 1);
			if (!limited) {
				return u"limit 1: parse failed"_q;
			} else if (!limited->hasNext) {
				return u"limit 1: expected hasNext true"_q;
			}
			return QString();
		} },
		{ u"api_traces_incoming_fixture"_q, [] {
			const auto name = u"api-ton-received-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::Transfer;
			expected.incoming = true;
			expected.counterparty = Raw(u"0:8B704249E018FAA59BDC356F463A7A34"
				u"FC0201DBBFA7441A387BE83E9FFFBFC4"_q);
			expected.amountNano = 250000000;
			expected.feeNano = 396408;
			expected.comment = u"На покупку Dust"_q;
			expected.date = 1745876068;
			expected.lt = 56572595000001ULL;
			expected.status = TransferItem::Status::Success;
			return CheckTracesItem(page->list.front(), expected);
		} },
		{ u"api_traces_incoming_min_fixture"_q, [] {
			const auto name = u"api-ton-received-acc2-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc2(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::Transfer;
			expected.incoming = true;
			expected.counterparty = Raw(u"0:CB04A1EC9CD6F0F795445DA1D21F3469"
				u"14C6FCBE3D3E5932F2477A88347BA492"_q);
			expected.amountNano = 1;
			expected.feeNano = 54;
			expected.date = 1760463682;
			expected.lt = 62557753000001ULL;
			expected.status = TransferItem::Status::Success;
			return CheckTracesItem(page->list.front(), expected);
		} },
		{ u"api_traces_contract_call_fixture"_q, [] {
			const auto name = u"api-contract-call-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::Transfer;
			expected.incoming = false;
			expected.counterparty = Raw(u"0:0E7AF74228AB4FBCD0AF55DD4CF2BA8B"
				u"718111ACC498DFD8E8BB953F810757C0"_q);
			expected.amountNano = 90000000;
			expected.feeNano = 7278029;
			expected.date = 1748275423;
			expected.lt = 57605165000001ULL;
			expected.status = TransferItem::Status::Success;
			const auto failure = CheckTracesItem(page->list.front(), expected);
			if (!failure.isEmpty()) {
				return failure;
			}
			const auto unknownName = u"api-contract-call-unknown-traces.json"_q;
			const auto unknownBytes = ReadFixture(unknownName);
			if (unknownBytes.isEmpty()) {
				return u"fixture read failed: "_q + unknownName;
			}
			const auto unknown = ParseTraces(unknownBytes, Acc1(), 20);
			if (!unknown) {
				return u"unknown: parse failed"_q;
			} else if (int(unknown->list.size()) != 1) {
				return u"unknown count: got "_q
					+ QString::number(unknown->list.size());
			}
			auto unknownExpected = TransferItem();
			unknownExpected.kind = TransferItem::Kind::Transfer;
			unknownExpected.incoming = false;
			unknownExpected.counterparty = Raw(u"0:78DFE54299FD7B3BB60C779D5F"
				u"02F3A370C902C38E8DFF724C0A87A01FF643A3"_q);
			unknownExpected.amountNano = 15000000;
			unknownExpected.feeNano = 2388337;
			unknownExpected.date = 1740428823;
			unknownExpected.lt = 54326932000001ULL;
			unknownExpected.status = TransferItem::Status::Success;
			const auto unknownFailure = CheckTracesItem(
				unknown->list.front(),
				unknownExpected);
			return unknownFailure.isEmpty()
				? QString()
				: (u"unknown: "_q + unknownFailure);
		} },
		{ u"api_traces_jetton_collapse_fixture"_q, [] {
			const auto name = u"api-ft-sent-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::ContractInteraction;
			expected.incoming = false;
			expected.counterparty = Raw(u"0:C4072D7B04AB4C504CD223DFDE42C803"
				u"7C6A99272D3624AEC2CBFF2E72B102DD"_q);
			expected.amountNano = 18000000;
			expected.feeNano = 2517337;
			expected.date = 1754158183;
			expected.lt = 60056271000001ULL;
			expected.status = TransferItem::Status::Success;
			const auto failure = CheckTracesItem(page->list.front(), expected);
			if (!failure.isEmpty()) {
				return failure;
			}
			const auto receivedName = u"api-ft-received-traces.json"_q;
			const auto receivedBytes = ReadFixture(receivedName);
			if (receivedBytes.isEmpty()) {
				return u"fixture read failed: "_q + receivedName;
			}
			const auto received = ParseTraces(receivedBytes, Acc1(), 20);
			if (!received) {
				return u"received: parse failed"_q;
			} else if (int(received->list.size()) != 1) {
				return u"received count: got "_q
					+ QString::number(received->list.size());
			} else if (received->list.front().kind
				!= TransferItem::Kind::ContractInteraction) {
				return u"received: expected ContractInteraction"_q;
			}
			return QString();
		} },
		{ u"api_traces_nft_collapse_fixture"_q, [] {
			const auto names = std::vector<QString>{
				u"api-nft-sent-traces.json"_q,
				u"api-nft-received-traces.json"_q,
			};
			for (const auto &name : names) {
				const auto bytes = ReadFixture(name);
				if (bytes.isEmpty()) {
					return u"fixture read failed: "_q + name;
				}
				const auto page = ParseTraces(bytes, Acc1(), 20);
				if (!page) {
					return name + u": parse failed"_q;
				} else if (int(page->list.size()) != 1) {
					return name + u": count "_q
						+ QString::number(page->list.size());
				} else if (page->list.front().kind
					!= TransferItem::Kind::ContractInteraction) {
					return name + u": expected ContractInteraction"_q;
				}
			}
			return QString();
		} },
		{ u"api_traces_pending_fixture"_q, [] {
			const auto name = u"api-pending-traces.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseTraces(bytes, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expected = TransferItem();
			expected.kind = TransferItem::Kind::Transfer;
			expected.incoming = false;
			expected.counterparty = Raw(u"0:4E3664FAEE814FBDB6C1CB36D72BAC52"
				u"D15E186C11ECD664FE1B3F1A7CFD801D"_q);
			expected.amountNano = 1000000000;
			expected.feeNano = 2062939;
			expected.date = 1754154889;
			expected.lt = 60054975000001ULL;
			expected.status = TransferItem::Status::Pending;
			const auto &item = page->list.front();
			const auto failure = CheckTracesItem(item, expected);
			if (!failure.isEmpty()) {
				return failure;
			}
			const auto traceId = QByteArray::fromBase64(
				"9QeaIiXlgf8UD9boljxboc15XqfaJ2EWXLfH94apqEc=");
			if (item.traceId != traceId) {
				return u"traceId mismatch"_q;
			}
			const auto extHashNorm = QByteArray::fromBase64(
				"JlAMiTCOjbxXFbDHl/5va9yqq0ffnn8As5NAFZDeyMg=");
			if (item.externalHashNorm != extHashNorm) {
				return u"externalHashNorm mismatch"_q;
			}
			return QString();
		} },
		{ u"api_traces_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("{\"traces\":{}}"),
				QByteArray("{\"traces\":[42]}"),
				QByteArray("{\"traces\":[{}]}"),
				QByteArray("{\"traces\":[{\"trace_id\":\"AA==\","
					"\"start_utime\":1,\"start_lt\":\"1\","
					"\"transactions\":\"x\"}]}"),
				QByteArray("{\"traces\":[{\"trace_id\":\"AA==\","
					"\"start_utime\":1,"
					"\"start_lt\":\"99999999999999999999999\","
					"\"transactions\":{}}]}"),
			};
			for (const auto &json : bad) {
				if (ParseTraces(json, Acc1(), 20)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			return QString();
		} },
		{ u"api_transactions_empty_fixtures"_q, [] {
			const auto names = std::vector<QString>{
				u"api-08-queryEncoding.json"_q,
				u"api-11-transactionsAlt.json"_q,
				u"api-12-transactionsFnd.json"_q,
			};
			for (const auto &name : names) {
				const auto bytes = ReadFixture(name);
				if (bytes.isEmpty()) {
					return u"fixture read failed: "_q + name;
				}
				const auto page = ParseTransactions(bytes, Acc1(), 20);
				if (!page) {
					return name + u": parse failed"_q;
				} else if (!page->list.empty()) {
					return name + u": expected empty list"_q;
				} else if (page->hasNext) {
					return name + u": expected hasNext false"_q;
				}
			}
			return QString();
		} },
		{ u"api_transactions_mapping"_q, [] {
			const auto json = QByteArray(
				"{\"transactions\":[{"
					"\"account\":\"0:9DA971AF38D2F03ABDF308D5F91636A97E5A2B"
						"07A66C39D71D7CBAE3B032EDDC\","
					"\"hash\":\"AA==\",\"lt\":\"100\",\"now\":1700000000,"
					"\"total_fees\":\"500\","
					"\"trace_id\":\"9QeaIiXlgf8UD9boljxboc15XqfaJ2EWXLfH94a"
						"pqEc=\","
					"\"description\":{\"aborted\":false,"
						"\"compute_ph\":{\"success\":true},"
						"\"action\":{\"success\":true}},"
					"\"in_msg\":{\"source\":\"0:4E3664FAEE814FBDB6C1CB36D72"
						"BAC52D15E186C11ECD664FE1B3F1A7CFD801D\","
						"\"value\":\"250\","
						"\"hash_norm\":\"JlAMiTCOjbxXFbDHl/5va9yqq0ffnn8As5"
							"NAFZDeyMg=\","
						"\"message_content\":{\"decoded\":{"
							"\"@type\":\"text_comment\","
							"\"comment\":\"hi\"}}},"
					"\"out_msgs\":[{\"destination\":\"0:4E3664FAEE814FBDB6C"
						"1CB36D72BAC52D15E186C11ECD664FE1B3F1A7CFD801D\","
						"\"value\":\"1000\",\"opcode\":null}]"
				"},{"
					"\"account\":\"0:4E3664FAEE814FBDB6C1CB36D72BAC52D15E18"
						"6C11ECD664FE1B3F1A7CFD801D\""
				"}]}");
			const auto page = ParseTransactions(json, Acc1(), 20);
			if (!page) {
				return u"parse failed"_q;
			} else if (int(page->list.size()) != 2) {
				return u"count: got "_q + QString::number(page->list.size());
			}
			auto expectedOut = TransferItem();
			expectedOut.kind = TransferItem::Kind::Transfer;
			expectedOut.incoming = false;
			expectedOut.counterparty = Raw(u"0:4E3664FAEE814FBDB6C1CB36D72B"
				u"AC52D15E186C11ECD664FE1B3F1A7CFD801D"_q);
			expectedOut.amountNano = 1000;
			expectedOut.feeNano = 500;
			expectedOut.date = 1700000000;
			expectedOut.lt = 100ULL;
			expectedOut.status = TransferItem::Status::Success;
			const auto &out = page->list.front();
			const auto outFailure = CheckTracesItem(out, expectedOut);
			if (!outFailure.isEmpty()) {
				return u"outgoing: "_q + outFailure;
			}
			const auto traceId = QByteArray::fromBase64(
				"9QeaIiXlgf8UD9boljxboc15XqfaJ2EWXLfH94apqEc=");
			if (out.traceId != traceId) {
				return u"traceId mismatch"_q;
			}
			const auto extHashNorm = QByteArray::fromBase64(
				"JlAMiTCOjbxXFbDHl/5va9yqq0ffnn8As5NAFZDeyMg=");
			if (out.externalHashNorm != extHashNorm) {
				return u"externalHashNorm mismatch"_q;
			}
			auto expectedIn = TransferItem();
			expectedIn.kind = TransferItem::Kind::Transfer;
			expectedIn.incoming = true;
			expectedIn.counterparty = Raw(u"0:4E3664FAEE814FBDB6C1CB36D72BA"
				u"C52D15E186C11ECD664FE1B3F1A7CFD801D"_q);
			expectedIn.amountNano = 250;
			expectedIn.feeNano = 500;
			expectedIn.comment = u"hi"_q;
			expectedIn.date = 1700000000;
			expectedIn.lt = 100ULL;
			expectedIn.status = TransferItem::Status::Success;
			const auto inFailure = CheckTracesItem(page->list[1], expectedIn);
			if (!inFailure.isEmpty()) {
				return u"incoming: "_q + inFailure;
			}
			if (ParseTransactions(
					QByteArray("{\"transactions\":\"x\"}"),
					Acc1(),
					20)) {
				return u"wrong type: expected nullopt"_q;
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
