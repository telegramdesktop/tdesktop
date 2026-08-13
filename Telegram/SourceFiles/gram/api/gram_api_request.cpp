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

std::optional<Address> ParseAddressValue(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto parsed = ParseAddress(value.toString());
	if (!parsed) {
		return std::nullopt;
	}
	return parsed->address;
}

std::optional<int64> ParseInt64String(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto result = value.toString().toLongLong(&ok);
	if (!ok) {
		return std::nullopt;
	}
	return result;
}

std::optional<quint64> ParseUint64String(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto result = value.toString().toULongLong(&ok);
	if (!ok) {
		return std::nullopt;
	}
	return result;
}

QByteArray DecodeAnyBase64(const QString &text) {
	auto normalized = text;
	normalized.replace(QChar('-'), QChar('+'));
	normalized.replace(QChar('_'), QChar('/'));
	return QByteArray::fromBase64(normalized.toLatin1());
}

QString PercentEncoded(const QString &value) {
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

bool ComputeSuccess(const QJsonObject &description) {
	const auto aborted = description.value(u"aborted"_q).toBool();
	const auto computeSuccess = description.value(u"compute_ph"_q)
		.toObject()
		.value(u"success"_q)
		.toBool();
	const auto actionSuccess = description.value(u"action"_q)
		.toObject()
		.value(u"success"_q)
		.toBool();
	return !aborted && computeSuccess && actionSuccess;
}

} // namespace Gram::ApiDetails
