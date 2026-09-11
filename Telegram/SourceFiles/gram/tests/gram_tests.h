/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>

#include <vector>

namespace Gram::Tests {

struct Check {
	QString name;
	Fn<QString()> run;
};

[[nodiscard]] inline QString CheckRequest(
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

[[nodiscard]] inline QByteArray ReadFixture(const QString &name) {
	auto file = QFile(
		QString::fromUtf8(GRAM_TEST_FIXTURES_PATH) + u"/"_q + name);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll().trimmed();
}

[[nodiscard]] std::vector<Check> ApiChecks();
[[nodiscard]] std::vector<Check> RatesChecks();
[[nodiscard]] std::vector<Check> NftChecks();
[[nodiscard]] std::vector<Check> StreamChecks();
[[nodiscard]] std::vector<Check> EmulateChecks();
[[nodiscard]] std::vector<Check> BocChecks();

} // namespace Gram::Tests
