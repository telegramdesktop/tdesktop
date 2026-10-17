/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/passcode_strength.h"

#include "ui/passcode_strength_translit.h"

#include <gsl/gsl>

#include <zxcvbn.h>

#include <array>
#include <cctype>
#include <cmath>

namespace Ui {
namespace {

constexpr auto kMinimumCharacters = 8;
constexpr auto kBitsPerDecimalDigit = 3.321928094887362;
constexpr auto kBitsForThousandGuesses = 3 * kBitsPerDecimalDigit;
constexpr auto kBitsForMillionGuesses = 6 * kBitsPerDecimalDigit;
constexpr auto kBitsForHundredMillionGuesses = 8 * kBitsPerDecimalDigit;
constexpr auto kBitsForTenBillionGuesses = 10 * kBitsPerDecimalDigit;
constexpr auto kDetailLength = int(ZXCVBN_DETAIL_LEN);
constexpr auto kTailClassCount = 5;
constexpr auto kBoundedCapacity = kDetailLength + kTailClassCount + 1;

struct BoundedCandidate {
	std::array<char, kBoundedCapacity> bytes{};
	int prefixLength = 0;
	int prefixClasses = 0;
	int tailLength = 0;
	int tailClasses = 0;
	int appended = 0;
	std::array<char, kTailClassCount> tailRepresentative{};
	char firstTailByte = 0;
};

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

[[nodiscard]] int ClassOfByte(char byte) {
	const auto value = int(uchar(byte));
	if (std::islower(value)) {
		return 1;
	} else if (std::isupper(value)) {
		return 2;
	} else if (std::isdigit(value)) {
		return 4;
	} else if (value <= 0x7F) {
		return 8;
	}
	return 16;
}

[[nodiscard]] int ClassIndex(int classBit) {
	auto result = 0;
	while ((1 << result) != classBit) {
		++result;
	}
	return result;
}

void PushByte(BoundedCandidate &candidate, char byte) {
	const auto classBit = ClassOfByte(byte);
	if (candidate.prefixLength < kDetailLength) {
		candidate.bytes[candidate.prefixLength++] = byte;
		candidate.prefixClasses |= classBit;
		return;
	}
	if (!candidate.tailLength++) {
		candidate.firstTailByte = byte;
	}
	if (!(candidate.tailClasses & classBit)) {
		candidate.tailClasses |= classBit;
		candidate.tailRepresentative[ClassIndex(classBit)] = byte;
	}
}

void PushCodePoint(BoundedCandidate &candidate, char32_t point) {
	if (point < 0x80) {
		PushByte(candidate, char(point));
	} else if (point < 0x800) {
		PushByte(candidate, char(0xC0 | (point >> 6)));
		PushByte(candidate, char(0x80 | (point & 0x3F)));
	} else if (point < 0x10000) {
		PushByte(candidate, char(0xE0 | (point >> 12)));
		PushByte(candidate, char(0x80 | ((point >> 6) & 0x3F)));
		PushByte(candidate, char(0x80 | (point & 0x3F)));
	} else {
		PushByte(candidate, char(0xF0 | (point >> 18)));
		PushByte(candidate, char(0x80 | ((point >> 12) & 0x3F)));
		PushByte(candidate, char(0x80 | ((point >> 6) & 0x3F)));
		PushByte(candidate, char(0x80 | (point & 0x3F)));
	}
}

void PushMapped(
		BoundedCandidate &candidate,
		const char *mapped,
		bool uppercaseFirst,
		bool uppercaseRest) {
	auto uppercase = uppercaseFirst;
	for (; *mapped; ++mapped) {
		PushByte(
			candidate,
			uppercase ? char(std::toupper(uchar(*mapped))) : *mapped);
		uppercase = uppercaseRest;
	}
}

[[nodiscard]] bool IsUppercaseCyrillicAt(
		const QString &passcode,
		int index) {
	return (index >= 0)
		&& (index < passcode.size())
		&& IsUppercaseCyrillic(passcode[index].unicode());
}

void PushUnit(
		BoundedCandidate &candidate,
		const QString &passcode,
		int index) {
	const auto unit = passcode[index].unicode();
	const auto mapped = TransliterateCyrillic(LowercaseCyrillic(unit));
	if (!mapped) {
		PushCodePoint(candidate, unit);
		return;
	}
	const auto uppercase = IsUppercaseCyrillic(unit);
	PushMapped(
		candidate,
		mapped,
		uppercase,
		uppercase
			&& (IsUppercaseCyrillicAt(passcode, index - 1)
				|| IsUppercaseCyrillicAt(passcode, index + 1)));
}

void AppendByte(BoundedCandidate &candidate, char byte) {
	candidate.bytes[candidate.prefixLength + candidate.appended++] = byte;
}

// The library matches only the first kDetailLength bytes and models the
// rest as one edge costing log2(2 * tailLength) + 1.75 bits, yet it sizes
// the alphabet over the whole string and SequenceMatch peeks the byte right
// after the prefix. Passing the prefix, that real byte first and then one
// real tail byte per alphabet class the prefix lacks reproduces every match
// and the alphabet exactly; the edge then costs log2(2 * appended), so adding
// log2(tailLength / appended) to the result and to the tail part makes the
// bounded call identical to a whole-string one.
void AppendTail(BoundedCandidate &candidate) {
	if (!candidate.tailLength) {
		return;
	}
	AppendByte(candidate, candidate.firstTailByte);
	const auto covered = candidate.prefixClasses
		| ClassOfByte(candidate.firstTailByte);
	for (auto index = 0; index != kTailClassCount; ++index) {
		const auto classBit = (1 << index);
		if ((candidate.tailClasses & classBit) && !(covered & classBit)) {
			AppendByte(candidate, candidate.tailRepresentative[index]);
		}
	}
}

void NormalizeAndBound(const QString &passcode, BoundedCandidate &result) {
	const auto size = int(passcode.size());
	for (auto i = 0; i != size; ++i) {
		const auto unit = passcode[i].unicode();
		if (!unit) {
			break;
		} else if (QChar::isHighSurrogate(unit)) {
			if ((i + 1 != size)
				&& QChar::isLowSurrogate(passcode[i + 1].unicode())) {
				PushCodePoint(
					result,
					QChar::surrogateToUcs4(unit, passcode[++i].unicode()));
			}
		} else if (!QChar::isLowSurrogate(unit)) {
			PushUnit(result, passcode, i);
		}
	}
	AppendTail(result);
}

[[nodiscard]] double CorrectTail(
		ZxcMatch_t *info,
		const BoundedCandidate &bounded) {
	if (!bounded.tailLength) {
		return 0.;
	}
	const auto correction = std::log2(
		double(bounded.tailLength) / bounded.appended);
	for (auto part = info; part != nullptr; part = part->Next) {
		if ((part->Type & 31) == LONG_PWD_MATCH) {
			part->Entrpy += correction;
			part->MltEnpy += correction;
			part->Length = bounded.tailLength;
		}
	}
	return correction;
}

void WipeCandidate(BoundedCandidate &candidate) {
	const auto data = reinterpret_cast<volatile char*>(&candidate);
	const auto size = int(sizeof(BoundedCandidate));
	for (auto i = 0; i != size; ++i) {
		data[i] = 0;
	}
}

} // namespace

PasscodeStrength EstimatePasscodeStrength(const QString &passcode) {
	if (CountCharacters(passcode) < kMinimumCharacters) {
		return {
			PasscodeStrengthBand::VeryWeak,
			PasscodeStrengthAdvice::TooShort,
		};
	}
	auto bounded = BoundedCandidate();
	auto info = (ZxcMatch_t*)nullptr;
	const auto guard = gsl::finally([&] {
		ZxcvbnFreeInfo(info);
		WipeCandidate(bounded);
	});
	NormalizeAndBound(passcode, bounded);
	auto bits = ZxcvbnMatch(bounded.bytes.data(), nullptr, &info);
	bits += CorrectTail(info, bounded);
	const auto band = BandFromBits(bits);
	return {
		band,
		(band == PasscodeStrengthBand::Strong)
			? PasscodeStrengthAdvice::Fine
			: AdviceFromMatches(info),
	};
}

} // namespace Ui
