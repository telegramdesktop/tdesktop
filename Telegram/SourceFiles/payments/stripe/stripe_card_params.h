/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "stripe/stripe_form_encodable.h"

namespace Stripe {

struct CardParams {
	QString number;
	quint32 expMonth = 0;
	quint32 expYear = 0;
	QString cvc;
	QString name;
	QString addressLine1;
	QString addressLine2;
	QString addressCity;
	QString addressState;
	QString addressZip;
	QString addressCountry;
	QString currency;

	[[nodiscard]] static QString rootObjectName();
	[[nodiscard]] std::map<QString, QString> formFieldValues() const;
};

} // namespace Stripe
