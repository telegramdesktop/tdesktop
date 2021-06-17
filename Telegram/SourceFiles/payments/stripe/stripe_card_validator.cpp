/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_card_validator.h"

#include <QtCore/QDate>

namespace Stripe {
namespace {

constexpr auto kMinCvcLength = 3;

struct BinRange {
	QString low;
	QString high;
	int length = 0;
	CardBrand brand = CardBrand::Unknown;
};

[[nodiscard]] const std::vector<BinRange> &AllRanges() {
	static auto kResult = std::vector<BinRange>{
		// Unknown
		{ "", "", 19, CardBrand::Unknown },
		// American Express
		{ "34", "34", 15, CardBrand::Amex },
		{ "37", "37", 15, CardBrand::Amex },
		// Diners Club
		{ "30", "30", 16, CardBrand::DinersClub },
		{ "36", "36", 14, CardBrand::DinersClub },
		{ "38", "39", 16, CardBrand::DinersClub },
		// Discover
		{ "60", "60", 16, CardBrand::Discover },
		{ "64", "65", 16, CardBrand::Discover },
		// JCB
		{ "35", "35", 16, CardBrand::JCB },
		// Mastercard
		{ "50", "59", 16, CardBrand::MasterCard },
		{ "22", "27", 16, CardBrand::MasterCard },
		{ "67", "67", 16, CardBrand::MasterCard }, // Maestro
		// UnionPay
		{ "62", "62", 16, CardBrand::UnionPay },
		{ "81", "81", 16, CardBrand::UnionPay },
		// Visa
		{ "40", "49", 16, CardBrand::Visa },
		{ "413600", "413600", 13, CardBrand::Visa },
		{ "444509", "444509", 13, CardBrand::Visa },
		{ "444509", "444509", 13, CardBrand::Visa },
		{ "444550", "444550", 13, CardBrand::Visa },
		{ "450603", "450603", 13, CardBrand::Visa },
		{ "450617", "450617", 13, CardBrand::Visa },
		{ "450628", "450629", 13, CardBrand::Visa },
		{ "450636", "450636", 13, CardBrand::Visa },
		{ "450640", "450641", 13, CardBrand::Visa },
		{ "450662", "450662", 13, CardBrand::Visa },
		{ "463100", "463100", 13, CardBrand::Visa },
		{ "476142", "476142", 13, CardBrand::Visa },
		{ "476143", "476143", 13, CardBrand::Visa },
		{ "492901", "492902", 13, CardBrand::Visa },
		{ "492920", "492920", 13, CardBrand::Visa },
		{ "492923", "492923", 13, CardBrand::Visa },
		{ "492928", "492930", 13, CardBrand::Visa },
		{ "492937", "492937", 13, CardBrand::Visa },
		{ "492939", "492939", 13, CardBrand::Visa },
		{ "492960", "492960", 13, CardBrand::Visa },
	};
	return kResult;
}

[[nodiscard]] bool BinRangeMatchesNumber(
		const BinRange &range,
		const QString &sanitized) {
	const auto minWithLow = std::min(sanitized.size(), range.low.size());
	if (sanitized.midRef(0, minWithLow).toInt()
		< range.low.midRef(0, minWithLow).toInt()) {
		return false;
	}
	const auto minWithHigh = std::min(sanitized.size(), range.high.size());
	if (sanitized.midRef(0, minWithHigh).toInt()
		> range.high.midRef(0, minWithHigh).toInt()) {
		return false;
	}
	return true;
}

[[nodiscard]] bool IsNumeric(const QString &value) {
	return QRegularExpression("^[0-9]*$").match(value).hasMatch();
}

[[nodiscard]] QString RemoveWhitespaces(QString value) {
	return value.replace(QRegularExpression("\\s"), QString());
}

[[nodiscard]] std::vector<BinRange> BinRangesForNumber(
		const QString &sanitized) {
	const auto &all = AllRanges();
	auto result = std::vector<BinRange>();
	result.reserve(all.size());
	for (const auto &range : all) {
		if (BinRangeMatchesNumber(range, sanitized)) {
			result.push_back(range);
		}
	}
	return result;
}

[[nodiscard]] BinRange MostSpecificBinRangeForNumber(
		const QString &sanitized) {
	auto possible = BinRangesForNumber(sanitized);
	const auto compare = [&](const BinRange &a, const BinRange &b) {
		if (sanitized.isEmpty()) {
			const auto aUnknown = (a.brand == CardBrand::Unknown);
			const auto bUnknown = (b.brand == CardBrand::Unknown);
			if (aUnknown && !bUnknown) {
				return true;
			} else if (!aUnknown && bUnknown) {
				return false;
			}
		}
		return a.low.size() < b.low.size();
	};
	std::sort(begin(possible), end(possible), compare);
	return possible.back();
}

[[nodiscard]] int MaxCvcLengthForBranch(CardBrand brand) {
	switch (brand) {
	case CardBrand::Amex:
	case CardBrand::Unknown:
		return 4;
	default:
		return 3;
	}
}

[[nodiscard]] std::vector<CardBrand> PossibleBrandsForNumber(
		const QString &sanitized) {
	const auto ranges = BinRangesForNumber(sanitized);
	auto result = std::vector<CardBrand>();
	for (const auto &range : ranges) {
		const auto brand = range.brand;
		if (brand == CardBrand::Unknown
			|| (std::find(begin(result), end(result), brand)
				!= end(result))) {
			continue;
		}
		result.push_back(brand);
	}
	return result;
}

[[nodiscard]] CardBrand BrandForNumber(const QString &number) {
	const auto sanitized = RemoveWhitespaces(number);
	if (!IsNumeric(sanitized)) {
		return CardBrand::Unknown;
	}
	const auto possible = PossibleBrandsForNumber(sanitized);
	return (possible.size() == 1) ? possible.front() : CardBrand::Unknown;
}

[[nodiscard]] bool IsValidLuhn(const QString &sanitized) {
	auto odd = true;
	auto sum = 0;
	for (auto i = sanitized.end(); i != sanitized.begin();) {
		--i;
		auto digit = int(i->unicode() - '0');
		odd = !odd;
		if (odd) {
			digit *= 2;
		}
		if (digit > 9) {
			digit -= 9;
		}
		sum += digit;
	}
	return (sum % 10) == 0;
}

} // namespace

CardValidationResult ValidateCard(const QString &number) {
	const auto sanitized = RemoveWhitespaces(number);
	if (!IsNumeric(sanitized)) {
		return { .state = ValidationState::Invalid };
	} else if (sanitized.isEmpty()) {
		return { .state = ValidationState::Incomplete };
	}
	const auto range = MostSpecificBinRangeForNumber(sanitized);
	const auto brand = range.brand;
	if (sanitized.size() > range.length) {
		return { .state = ValidationState::Invalid, .brand = brand };
	} else if (sanitized.size() < range.length) {
		return { .state = ValidationState::Incomplete, .brand = brand };
	} else if (!IsValidLuhn(sanitized)) {
		return { .state = ValidationState::Invalid, .brand = brand };
	}
	return {
		.state = ValidationState::Valid,
		.brand = brand,
		.finished = true,
	};
}

ExpireDateValidationResult ValidateExpireDate(const QString &date) {
	const auto sanitized = RemoveWhitespaces(date).replace('/', QString());
	if (!IsNumeric(sanitized)) {
		return { ValidationState::Invalid };
	} else if (sanitized.size() < 2) {
		return { ValidationState::Incomplete };
	}
	const auto normalized = (sanitized[0] > '1' ? "0" : "") + sanitized;
	const auto month = normalized.mid(0, 2).toInt();
	if (month < 1 || month > 12) {
		return { ValidationState::Invalid };
	} else if (normalized.size() < 4) {
		return { ValidationState::Incomplete };
	} else if (normalized.size() > 4) {
		return { ValidationState::Invalid };
	}
	const auto year = 2000 + normalized.mid(2).toInt();

	const auto currentDate = QDate::currentDate();
	const auto currentMonth = currentDate.month();
	const auto currentYear = currentDate.year();
	if (year < currentYear) {
		return { ValidationState::Invalid };
	} else if (year == currentYear && month < currentMonth) {
		return { ValidationState::Invalid };
	}
	return { ValidationState::Valid, true };
}

ValidationState ValidateParsedExpireDate(
		quint32 month,
		quint32 year) {
	if ((year / 100) != 20) {
		return ValidationState::Invalid;
	}
	return ValidateExpireDate(
		QString("%1%2"
		).arg(month, 2, 10, QChar('0')
		).arg(year % 100, 2, 10, QChar('0'))
	).state;
}

CvcValidationResult ValidateCvc(
		const QString &number,
		const QString &cvc) {
	if (!IsNumeric(cvc)) {
		return { ValidationState::Invalid };
	} else if (cvc.size() < kMinCvcLength) {
		return { ValidationState::Incomplete };
	}
	const auto maxLength = MaxCvcLengthForBranch(BrandForNumber(number));
	if (cvc.size() > maxLength) {
		return { ValidationState::Invalid };
	}
	return { ValidationState::Valid, (cvc.size() == maxLength) };
}

std::vector<int> CardNumberFormat(const QString &number) {
	static const auto kDefault = std::vector{ 4, 4, 4, 4 };
	const auto sanitized = RemoveWhitespaces(number);
	if (!IsNumeric(sanitized)) {
		return kDefault;
	}
	const auto range = MostSpecificBinRangeForNumber(sanitized);
	if (range.brand == CardBrand::DinersClub && range.length == 14) {
		return { 4, 6, 4 };
	} else if (range.brand == CardBrand::Amex) {
		return { 4, 6, 5 };
	}
	return kDefault;
}

} // namespace Stripe
