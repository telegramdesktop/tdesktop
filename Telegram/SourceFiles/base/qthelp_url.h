/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace qthelp {

inline QString url_encode(const QString &part) {
	return QString::fromLatin1(QUrl::toPercentEncoding(part));
}

inline QString url_decode(const QString &encoded) {
	return QUrl::fromPercentEncoding(encoded.toUtf8());
}

enum class UrlParamNameTransform {
	NoTransform,
	ToLower,
};
// Parses a string like "p1=v1&p2=v2&..&pn=vn" to a map.
QMap<QString, QString> url_parse_params(
	const QString &params,
	UrlParamNameTransform transform = UrlParamNameTransform::NoTransform);

QString url_append_query_or_hash(const QString &url, const QString &add);

bool is_ipv6(const QString &ip);

} // namespace qthelp
