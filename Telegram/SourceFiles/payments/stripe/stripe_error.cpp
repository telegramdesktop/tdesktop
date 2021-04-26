/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_error.h"

#include "stripe/stripe_decode.h"

namespace Stripe {

Error::Code Error::code() const {
	return _code;
}

QString Error::description() const {
	return _description;
}

QString Error::message() const {
	return _message;
}

QString Error::parameter() const {
	return _parameter;
}

Error Error::None() {
	return Error(Code::None, {}, {}, {});
}

Error Error::DecodedObjectFromResponse(QJsonObject object) {
	const auto entry = object.value("error");
	if (!entry.isObject()) {
		return Error::None();
	}
	const auto error = entry.toObject();
	const auto string = [&](QStringView key) {
		return error.value(key).toString();
	};
	const auto type = string(u"type");
	const auto message = string(u"message");
	const auto parameterSnakeCase = string(u"param");

	// There should always be a message and type for the error
	if (message.isEmpty() || type.isEmpty()) {
		return {
			Code::API,
			"GenericError",
			"Could not interpret the error response "
			"that was returned from Stripe."
		};
	}

	auto parameterWords = parameterSnakeCase.isEmpty()
		? QStringList()
		: parameterSnakeCase.split('_', Qt::SkipEmptyParts);
	auto first = true;
	for (auto &word : parameterWords) {
		if (first) {
			first = false;
		} else {
			word = word[0].toUpper() + word.midRef(1);
		}
	}
	const auto parameter = parameterWords.join(QString());
	if (type == "api_error") {
		return { Code::API, "GenericError", message, parameter };
	} else if (type == "invalid_request_error") {
		return { Code::InvalidRequest, "GenericError", message, parameter };
	} else if (type != "card_error") {
		return { Code::Unknown, type, message, parameter };
	}
	const auto code = string(u"code");
	const auto cardError = [&](const QString &description) {
		return Error{ Code::Card, description, message, parameter };
	};
	if (code == "incorrect_number") {
		return cardError("IncorrectNumber");
	} else if (code == "invalid_number") {
		return cardError("InvalidNumber");
	} else if (code == "invalid_expiry_month") {
		return cardError("InvalidExpiryMonth");
	} else if (code == "invalid_expiry_year") {
		return cardError("InvalidExpiryYear");
	} else if (code == "invalid_cvc") {
		return cardError("InvalidCVC");
	} else if (code == "expired_card") {
		return cardError("ExpiredCard");
	} else if (code == "incorrect_cvc") {
		return cardError("IncorrectCVC");
	} else if (code == "card_declined") {
		return cardError("CardDeclined");
	} else if (code == "processing_error") {
		return cardError("ProcessingError");
	} else {
		return cardError(code);
	}
}

bool Error::empty() const {
	return (_code == Code::None);
}

} // namespace Stripe
