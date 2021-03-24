/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "stripe/stripe_form_encodable.h"

namespace Stripe {

class CardParams final : public FormEncodable {
public:
	QString RootObjectName() const override;
	std::map<QString, QString> formFieldValues() const override;

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
};

} // namespace Stripe
