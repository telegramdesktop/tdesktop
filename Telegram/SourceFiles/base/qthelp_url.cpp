/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/qthelp_url.h"

namespace qthelp {

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

} // namespace qthelp
