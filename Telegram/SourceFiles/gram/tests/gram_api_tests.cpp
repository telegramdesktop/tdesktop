/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_request.h"

#include <vector>

namespace Gram::Tests {

std::vector<Check> ApiChecks() {
	return {
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
