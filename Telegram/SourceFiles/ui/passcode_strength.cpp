/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/passcode_strength.h"

#include <gsl/gsl>

#include <zxcvbn.h>

namespace Ui {
namespace {

constexpr auto kMinimumCharacters = 8;
constexpr auto kBitsPerDecimalDigit = 3.321928094887362;
constexpr auto kBitsForThousandGuesses = 3 * kBitsPerDecimalDigit;
constexpr auto kBitsForMillionGuesses = 6 * kBitsPerDecimalDigit;
constexpr auto kBitsForHundredMillionGuesses = 8 * kBitsPerDecimalDigit;
constexpr auto kBitsForTenBillionGuesses = 10 * kBitsPerDecimalDigit;

[[nodiscard]] int CountCharacters(const QString &passcode) {
	const auto size = int(passcode.size());
	auto result = 0;
	for (auto i = 0; i != size; ++i) {
		++result;
		if (passcode[i].isHighSurrogate()
			&& (i + 1 != size)
			&& passcode[i + 1].isLowSurrogate()) {
			++i;
		}
	}
	return result;
}

[[nodiscard]] PasscodeStrengthBand BandFromBits(double bits) {
	const auto score = (bits < kBitsForThousandGuesses)
		? 0
		: (bits < kBitsForMillionGuesses)
		? 1
		: (bits < kBitsForHundredMillionGuesses)
		? 2
		: (bits < kBitsForTenBillionGuesses)
		? 3
		: 4;
	return (score < 2)
		? PasscodeStrengthBand::Weak
		: (score < 4)
		? PasscodeStrengthBand::Good
		: PasscodeStrengthBand::Strong;
}

[[nodiscard]] PasscodeStrengthAdvice AdviceFromMatches(ZxcMatch_t *info) {
	auto longest = (ZxcMatch_t*)nullptr;
	for (auto part = info; part != nullptr; part = part->Next) {
		if (!longest) {
			longest = part;
			continue;
		}
		const auto better = (part->Length != longest->Length)
			? (part->Length > longest->Length)
			: (part->Entrpy != longest->Entrpy)
			? (part->Entrpy < longest->Entrpy)
			: (part->Begin < longest->Begin);
		if (better) {
			longest = part;
		}
	}
	if (!longest) {
		return PasscodeStrengthAdvice::AddVariety;
	}
	switch (ZxcTypeMatch_t(longest->Type & 31)) {
	case DICTIONARY_MATCH:
	case USER_MATCH:
		return PasscodeStrengthAdvice::CommonWord;
	case DICT_LEET_MATCH:
	case USER_LEET_MATCH:
		return PasscodeStrengthAdvice::LeetWord;
	case SEQUENCE_MATCH:
		return PasscodeStrengthAdvice::Sequence;
	case REPEATS_MATCH:
		return PasscodeStrengthAdvice::Repeat;
	case SPATIAL_MATCH:
		return PasscodeStrengthAdvice::Keyboard;
	case DATE_MATCH:
	case YEAR_MATCH:
		return PasscodeStrengthAdvice::DateLike;
	case NON_MATCH:
	case BRUTE_MATCH:
	case LONG_PWD_MATCH:
	case MULTIPLE_MATCH:
		return PasscodeStrengthAdvice::AddVariety;
	}
	return PasscodeStrengthAdvice::AddVariety;
}

void WipeBytes(QByteArray &bytes) {
	const auto size = int(bytes.size());
	const auto data = static_cast<volatile char*>(bytes.data());
	for (auto i = 0; i != size; ++i) {
		data[i] = 0;
	}
	bytes.clear();
}

} // namespace

PasscodeStrength EstimatePasscodeStrength(const QString &passcode) {
	if (CountCharacters(passcode) < kMinimumCharacters) {
		return {
			PasscodeStrengthBand::VeryWeak,
			PasscodeStrengthAdvice::TooShort,
		};
	}
	auto utf8 = passcode.toUtf8();
	auto info = (ZxcMatch_t*)nullptr;
	const auto guard = gsl::finally([&] {
		ZxcvbnFreeInfo(info);
		WipeBytes(utf8);
	});
	const auto bits = ZxcvbnMatch(utf8.constData(), nullptr, &info);
	const auto band = BandFromBits(bits);
	return {
		band,
		(band == PasscodeStrengthBand::Strong)
			? PasscodeStrengthAdvice::Fine
			: AdviceFromMatches(info),
	};
}

} // namespace Ui
