/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/qthelp_url.h"

namespace qthelp {
namespace {

QRegularExpression RegExpProtocol() {
	static const auto result = QRegularExpression("^([a-zA-Z]+)://");
	return result;
}

bool IsGoodProtocol(const QString &protocol) {
	const auto equals = [&](QLatin1String string) {
		return protocol.compare(string, Qt::CaseInsensitive) == 0;
	};
	return equals(qstr("http"))
		|| equals(qstr("https"))
		|| equals(qstr("tg"));
}

} // namespace

QMap<QString, QString> url_parse_params(
		const QString &params,
		UrlParamNameTransform transform) {
	auto result = QMap<QString, QString>();

	const auto transformParamName = [transform](const QString &name) {
		if (transform == UrlParamNameTransform::ToLower) {
			return name.toLower();
		}
		return name;
	};
	for (const auto &param : params.split('&')) {
		// Skip params without a name (starting with '=').
		if (auto separatorPosition = param.indexOf('=')) {
			const auto paramName = transformParamName(
				(separatorPosition > 0)
					? param.mid(0, separatorPosition)
					: param);
			const auto paramValue = (separatorPosition > 0)
				? url_decode(param.mid(separatorPosition + 1))
				: QString();
			if (!result.contains(paramName)) {
				result.insert(paramName, paramValue);
			}
		}
	}
	return result;
}

bool is_ipv6(const QString &ip) {
	//static const auto regexp = QRegularExpression("^[a-fA-F0-9:]+$");
	//return regexp.match(ip).hasMatch();
	return ip.indexOf('.') < 0 && ip.indexOf(':') >= 0;
}

QString url_append_query_or_hash(const QString &url, const QString &add) {
	const auto query = url.lastIndexOf('?');
	if (query < 0) {
		return url + '?' + add;
	}
	const auto hash = url.lastIndexOf('#');
	return url
		+ (query >= 0 && query > url.lastIndexOf('#') ? '&' : '?')
		+ add;
}

QString validate_url(const QString &value) {
	const auto trimmed = value.trimmed();
	if (trimmed.isEmpty()) {
		return QString();
	}
	const auto match = TextUtilities::RegExpDomainExplicit().match(trimmed);
	if (!match.hasMatch()) {
		const auto domain = TextUtilities::RegExpDomain().match(trimmed);
		if (!domain.hasMatch() || domain.capturedStart() != 0) {
			return QString();
		}
		return qstr("http://") + trimmed;
	} else if (match.capturedStart() != 0) {
		return QString();
	}
	const auto protocolMatch = RegExpProtocol().match(trimmed);
	Assert(protocolMatch.hasMatch());
	return IsGoodProtocol(protocolMatch.captured(1)) ? trimmed : QString();
}

} // namespace qthelp
