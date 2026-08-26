/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_request.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QUrl>

namespace Gram {

std::optional<ApiError> ParseApiError(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto error = object.value(u"error"_q);
	if (!error.isString()) {
		return std::nullopt;
	}
	return ApiError{ 0, error.toString() };
}

} // namespace Gram

namespace Gram::ApiDetails {

QString PercentEncoded(const QString &value) {
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace Gram::ApiDetails
