/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "stripe/stripe_address.h"

#include <QtCore/QString>

namespace Stripe {

struct PaymentConfiguration {
	QString publishableKey;
	// PaymentMethodType additionalPaymentMethods; // Apply Pay

	// TODO incomplete, not used.
	//BillingAddressFields requiredBillingAddressFields
	//	= BillingAddressFields::None;

	QString companyName;
	// QString appleMerchantIdentifier; // Apple Pay
	// bool smsAutofillDisabled = true; // Mobile only
};

} // namespace Stripe
