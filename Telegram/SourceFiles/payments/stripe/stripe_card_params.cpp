/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_card_params.h"

namespace Stripe {

QString CardParams::rootObjectName() {
	return "card";
}

std::map<QString, QString> CardParams::formFieldValues() const {
	return {
		{ "number", number },
		{ "cvc", cvc },
		{ "name", name },
		{ "address_line1", addressLine1 },
		{ "address_line2", addressLine2 },
		{ "address_city", addressCity },
		{ "address_state", addressState },
		{ "address_zip", addressZip },
		{ "address_country", addressCountry },
		{ "exp_month", QString::number(expMonth) },
		{ "exp_year", QString::number(expYear) },
		{ "currency", currency },
	};
}

} // namespace Stripe
