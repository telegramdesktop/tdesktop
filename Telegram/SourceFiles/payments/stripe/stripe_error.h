/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

class QJsonObject;

namespace Stripe {

class Error {
public:
	enum class Code {
		None = 0, // Non-Stripe errors.
		JsonParse = -1,
		JsonFormat = -2,
		Network = -3,

		Unknown = 8,
		Connection = 40, // Trouble connecting to Stripe.
		InvalidRequest = 50, // Your request had invalid parameters.
		API = 60, // General-purpose API error (should be rare).
		Card = 70, // Something was wrong with the given card (most common).
		Cancellation = 80, // The operation was cancelled.
		CheckoutUnknown = 5000, // Checkout failed
		CheckoutTooManyAttempts = 5001, // Too many incorrect code attempts
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

} // namespace Stripe
