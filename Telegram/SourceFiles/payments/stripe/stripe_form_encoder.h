/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "stripe/stripe_form_encodable.h"

namespace Stripe {

class FormEncoder {
public:
	[[nodiscard]] static QByteArray formEncodedDataForObject(
		FormEncodable &&object);

};

} // namespace Stripe
