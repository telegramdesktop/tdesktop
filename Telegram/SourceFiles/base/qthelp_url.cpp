/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

} // namespace qthelp
