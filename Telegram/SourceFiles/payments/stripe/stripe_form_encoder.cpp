/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_form_encoder.h"

#include <QStringList>
#include <QUrl>

namespace Stripe {

QByteArray FormEncoder::formEncodedDataForObject(
		FormEncodable &&object) {
	const auto root = object.rootObjectName();
	const auto values = object.formFieldValues();
	auto result = QByteArray();
	auto keys = std::vector<QString>();
	for (const auto &[key, value] : values) {
		if (!value.isEmpty()) {
			keys.push_back(key);
		}
	}
	std::sort(begin(keys), end(keys));
	const auto encode = [](const QString &value) {
		return QUrl::toPercentEncoding(value);
	};
	for (const auto &key : keys) {
		const auto fullKey = root.isEmpty() ? key : (root + '[' + key + ']');
		if (!result.isEmpty()) {
			result += '&';
		}
		result += encode(fullKey) + '=' + encode(values.at(key));
	}
	return result;
}

} // namespace Stripe
