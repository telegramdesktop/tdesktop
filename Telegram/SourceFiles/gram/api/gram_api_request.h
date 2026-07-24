/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

class QJsonObject;
class QJsonValue;

namespace Gram {

struct HttpRequest {
	bool post = false;
	QString endpoint;
	QString query;
	QByteArray payload;
};

struct ApiError {
	int code = 0;
	QString message;
};

[[nodiscard]] std::optional<ApiError> ParseApiError(
	const QByteArray &json);

} // namespace Gram

namespace Gram::ApiDetails {

[[nodiscard]] std::optional<int64> ParseInt64String(
	const QJsonValue &value);
[[nodiscard]] std::optional<quint64> ParseUint64String(
	const QJsonValue &value);
[[nodiscard]] QByteArray DecodeAnyBase64(const QString &text);
[[nodiscard]] QString PercentEncoded(const QString &value);
[[nodiscard]] bool ComputeSuccess(const QJsonObject &description);

} // namespace Gram::ApiDetails
