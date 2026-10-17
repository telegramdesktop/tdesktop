/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Ui {

enum class PasscodeStrengthBand : uchar {
	VeryWeak,
	Weak,
	Good,
	Strong,
};

enum class PasscodeStrengthAdvice : uchar {
	TooShort,
	CommonWord,
	LeetWord,
	Sequence,
	Repeat,
	Keyboard,
	DateLike,
	AddVariety,
	Fine,
};

struct PasscodeStrength {
	PasscodeStrengthBand band = PasscodeStrengthBand::VeryWeak;
	PasscodeStrengthAdvice advice = PasscodeStrengthAdvice::TooShort;
};

[[nodiscard]] PasscodeStrength EstimatePasscodeStrength(
	const QString &passcode);

} // namespace Ui
