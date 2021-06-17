/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
