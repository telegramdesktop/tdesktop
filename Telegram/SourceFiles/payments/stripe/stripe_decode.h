/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QJsonObject>
#include <vector>

namespace Stripe {

[[nodiscard]] bool ContainsFields(
	const QJsonObject &object,
	std::vector<QStringView> keys);

} // namespace Stripe
