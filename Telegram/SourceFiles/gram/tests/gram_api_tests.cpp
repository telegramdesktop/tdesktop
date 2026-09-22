/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_request.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <vector>

namespace Gram::Tests {
namespace {

[[nodiscard]] QByteArray PublicKeyResponse(
		const QJsonValue &number,
		bool objectEntry) {
	const auto entry = objectEntry
		? QJsonValue(QJsonObject{
			{ u"type"_q, u"num"_q },
			{ u"value"_q, number },
		})
		: QJsonValue(QJsonArray{ u"num"_q, number });
	return QJsonDocument(QJsonObject{
		{ u"ok"_q, true },
		{ u"result"_q, QJsonObject{
			{ u"exit_code"_q, 0 },
			{ u"stack"_q, QJsonArray{ entry } },
		} },
	}).toJson(QJsonDocument::Compact);
}

} // namespace

std::vector<Check> ApiChecks() {
	return {
		{ u"wallet_public_key_request"_q, [] {
			for (const auto &address : { u"0:abcd"_q, u"quoted\"\\\n"_q }) {
				const auto request = WalletPublicKeyRequest(address);
				if (!request.post
					|| request.endpoint != u"/api/v2/jsonRPC"_q
					|| !request.query.isEmpty()) {
					return u"invalid public-key request routing"_q;
				}
				const auto document = QJsonDocument::fromJson(request.payload);
				const auto body = document.object();
				const auto params = body.value(u"params"_q).toObject();
				if (body.value(u"jsonrpc"_q).toString() != u"2.0"_q
					|| body.value(u"id"_q).toString().isEmpty()
					|| body.value(u"method"_q).toString() != u"runGetMethod"_q
					|| params.value(u"address"_q).toString() != address
					|| params.value(u"method"_q).toString() != u"get_public_key"_q
					|| !params.value(u"stack"_q).isArray()
					|| !params.value(u"stack"_q).toArray().isEmpty()) {
					return u"invalid public-key JSON-RPC request"_q;
				}
			}
			return QString();
		} },
		{ u"wallet_public_key_uint256"_q, [] {
			struct Case {
				QString number;
				QByteArray key;
			};
			const auto one = QByteArray(31, '\0') + QByteArray(1, char(1));
			const auto max = QByteArray(32, char(0xFF));
			const auto cases = std::vector<Case>{
				{ u"1"_q, one },
				{ u"00001"_q, one },
				{ u"0x1"_q, one },
				{ u"0x"_q + QString(80, QChar('0')) + QChar('1'), one },
				{ u"256"_q,
					QByteArray(30, '\0') + QByteArray::fromHex("0100") },
				{ u"0x"_q + QString(64, QChar('F')), max },
				{ u"115792089237316195423570985008687907853269984665640564"
					u"039457584007913129639935"_q, max },
				{ u"0x8123456789abcdef0123456789abcdef"
					u"0123456789abcdef0123456789abcdef"_q,
					QByteArray::fromHex("8123456789abcdef0123456789abcdef"
						"0123456789abcdef0123456789abcdef") },
			};
			for (const auto &entry : cases) {
				for (const auto object : { false, true }) {
					const auto key = ParseWalletPublicKey(
						PublicKeyResponse(entry.number, object));
					if (!key || *key != entry.key) {
						return u"incorrect uint256: "_q + entry.number;
					}
				}
			}
			const auto jsonRpc = QByteArray(
				R"({"jsonrpc":"2.0","result":{"exit_code":0,)"
				R"("stack":[["num","0x1"]]}})");
			const auto key = ParseWalletPublicKey(jsonRpc);
			return (key && *key == one)
				? QString()
				: u"JSON-RPC response without ok was rejected"_q;
		} },
		{ u"wallet_public_key_invalid_numbers"_q, [] {
			const auto bad = std::vector<QString>{
				u""_q, u"0"_q, u"0000"_q, u"0x"_q, u"0x0"_q,
				u"-1"_q, u"-0x1"_q, u"+1"_q, u" 1"_q, u"1 "_q,
				u"1.0"_q, u"1e2"_q, u"0xg"_q, u"0x-1"_q, u"\u0661"_q,
				u"0x1"_q + QString(64, QChar('0')),
				u"115792089237316195423570985008687907853269984665640564"
					u"039457584007913129639936"_q,
			};
			for (const auto &number : bad) {
				for (const auto object : { false, true }) {
					if (ParseWalletPublicKey(PublicKeyResponse(number, object))) {
						return u"accepted invalid uint256: "_q + number;
					}
				}
			}
			for (const auto number : { QJsonValue(1), QJsonValue(true) }) {
				for (const auto object : { false, true }) {
					if (ParseWalletPublicKey(PublicKeyResponse(number, object))) {
						return u"accepted non-string uint256"_q;
					}
				}
			}
			return QString();
		} },
		{ u"wallet_public_key_invalid_responses"_q, [] {
			const auto bad = std::vector<QByteArray>{
				"", "{", "[]", "{}", "null",
				R"({"ok":false,"result":{"exit_code":0,"stack":[["num","1"]]}})",
				R"({"ok":1,"result":{"exit_code":0,"stack":[["num","1"]]}})",
				R"({"error":null,"result":{"exit_code":0,"stack":[["num","1"]]}})",
				R"({"error":{"code":1},"result":{"exit_code":0,"stack":[["num","1"]]}})",
				R"({"result":{"stack":[["num","1"]]}})",
				R"({"result":{"exit_code":11,"stack":[["num","1"]]}})",
				R"({"result":{"exit_code":0.5,"stack":[["num","1"]]}})",
				R"({"result":{"exit_code":"0","stack":[["num","1"]]}})",
				R"({"result":{"exit_code":false,"stack":[["num","1"]]}})",
				R"({"result":{"exit_code":0,"stack":[]}})",
				R"({"result":{"exit_code":0,"stack":[["num","1"],["num","2"]]}})",
				R"({"result":{"exit_code":0,"stack":[["cell","1"]]}})",
				R"({"result":{"exit_code":0,"stack":[["num","1",0]]}})",
				R"({"result":{"exit_code":0,"stack":[{"type":"cell","value":"1"}]}})",
				R"({"result":{"exit_code":0,"stack":[{"type":"num"}]}})",
			};
			for (const auto &json : bad) {
				if (ParseWalletPublicKey(json)) {
					return u"accepted invalid public-key response: "_q
						+ QString::fromUtf8(json);
				}
			}
			return QString();
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
	};
}

} // namespace Gram::Tests
