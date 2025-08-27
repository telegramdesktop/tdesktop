/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "stripe/stripe_card.h"
#include <vector>
#include <optional>

class QDate;

namespace Stripe {

enum class ValidationState {
	Invalid,
	Incomplete,
	Valid,
};

struct CardValidationResult {
	ValidationState state = ValidationState::Invalid;
	CardBrand brand = CardBrand::Unknown;
	bool finished = false;
};

[[nodiscard]] CardValidationResult ValidateCard(const QString &number);

struct ExpireDateValidationResult {
	ValidationState state = ValidationState::Invalid;
	bool finished = false;
};

[[nodiscard]] ExpireDateValidationResult ValidateExpireDate(
	const QString &date,
	const std::optional<QDate> &overrideExpireDateThreshold);

[[nodiscard]] ValidationState ValidateParsedExpireDate(
	quint32 month,
	quint32 year,
	const std::optional<QDate> &overrideExpireDateThreshold);

struct CvcValidationResult {
	ValidationState state = ValidationState::Invalid;
	bool finished = false;
};

[[nodiscard]] CvcValidationResult ValidateCvc(
	const QString &number,
	const QString &cvc);

[[nodiscard]] std::vector<int> CardNumberFormat(const QString &number);

} // namespace Stripe
