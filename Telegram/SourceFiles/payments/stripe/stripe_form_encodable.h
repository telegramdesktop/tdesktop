/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <map>

namespace Stripe {

class FormEncodable {
public:
	[[nodiscard]] virtual QString RootObjectName() const = 0;

	// TODO incomplete, not used: nested complex structures not supported.
	[[nodiscard]] virtual std::map<QString, QString> formFieldValues() const
		= 0;
};

} // namespace Stripe
