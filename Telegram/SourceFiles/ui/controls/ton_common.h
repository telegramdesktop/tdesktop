/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace Ui {

class InputField;

inline constexpr auto kNanosInOne = 1'000'000'000LL;

struct FormattedTonAmount {
	QString wholeString;
	QString separator;
	QString nanoString;
	QString full;
};

enum class TonFormatFlag {
	Signed = 0x01,
	Rounded = 0x02,
	Simple = 0x04,
};
constexpr bool is_flag_type(TonFormatFlag) { return true; };
using TonFormatFlags = base::flags<TonFormatFlag>;

[[nodiscard]] FormattedTonAmount FormatTonAmount(
	int64 amount,
	TonFormatFlags flags = TonFormatFlags());
[[nodiscard]] std::optional<int64> ParseTonAmountString(
	const QString &amount);

[[nodiscard]] QString TonAmountSeparator();

[[nodiscard]] not_null<Ui::InputField*> CreateTonAmountInput(
	not_null<QWidget*> parent,
	rpl::producer<QString> placeholder,
	int64 amount = 0);

} // namespace Ui
