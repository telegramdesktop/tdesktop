/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

class QJsonObject;

namespace SmartGlocal {

class Error {
public:
	enum class Code {
		None = 0, // Non-SmartGlocal errors.
		JsonParse = -1,
		JsonFormat = -2,
		Network = -3,

		Unknown = 8,
	};

	Error(
		Code code,
		const QString &description,
		const QString &message,
		const QString &parameter = QString())
	: _code(code)
	, _description(description)
	, _message(message)
	, _parameter(parameter) {
	}

	[[nodiscard]] Code code() const;
	[[nodiscard]] QString description() const;
	[[nodiscard]] QString message() const;
	[[nodiscard]] QString parameter() const;

	[[nodiscard]] static Error None();
	[[nodiscard]] static Error DecodedObjectFromResponse(QJsonObject object);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

private:
	Code _code = Code::None;
	QString _description;
	QString _message;
	QString _parameter;

};

} // namespace SmartGlocal
