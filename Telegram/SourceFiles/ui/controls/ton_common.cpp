/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/ton_common.h"

#include "base/qthelp_url.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
//#include "styles/style_wallet.h"

#include <QtCore/QLocale>

namespace Ui {
namespace {

constexpr auto kOneTon = kNanosInOne;
constexpr auto kNanoDigits = 9;

struct FixedAmount {
	QString text;
	int position = 0;
};

std::optional<int64> ParseAmountTons(const QString &trimmed) {
	auto ok = false;
	const auto grams = int64(trimmed.toLongLong(&ok));
	return (ok
		&& (grams <= std::numeric_limits<int64>::max() / kOneTon)
		&& (grams >= std::numeric_limits<int64>::min() / kOneTon))
		? std::make_optional(grams * kOneTon)
		: std::nullopt;
}

std::optional<int64> ParseAmountNano(QString trimmed) {
	while (trimmed.size() < kNanoDigits) {
		trimmed.append('0');
	}
	auto zeros = 0;
	for (const auto ch : trimmed) {
		if (ch == '0') {
			++zeros;
		} else {
			break;
		}
	}
	if (zeros == trimmed.size()) {
		return 0;
	} else if (trimmed.size() > kNanoDigits) {
		return std::nullopt;
	}
	auto ok = false;
	const auto value = trimmed.mid(zeros).toLongLong(&ok);
	return (ok && value > 0 && value < kOneTon)
		? std::make_optional(value)
		: std::nullopt;
}

[[nodiscard]] FixedAmount FixTonAmountInput(
		const QString &was,
		const QString &text,
		int position) {
	constexpr auto kMaxDigitsCount = 9;
	const auto separator = FormatTonAmount(1).separator;

	auto result = FixedAmount{ text, position };
	if (text.isEmpty()) {
		return result;
	} else if (text.startsWith('.')
		|| text.startsWith(',')
		|| text.startsWith(separator)) {
		result.text.prepend('0');
		++result.position;
	}
	auto separatorFound = false;
	auto digitsCount = 0;
	for (auto i = 0; i != result.text.size();) {
		const auto ch = result.text[i];
		const auto atSeparator = QStringView(result.text).mid(i).startsWith(separator);
		if (ch >= '0' && ch <= '9' && digitsCount < kMaxDigitsCount) {
			++i;
			++digitsCount;
			continue;
		} else if (!separatorFound
			&& (atSeparator || ch == '.' || ch == ',')) {
			separatorFound = true;
			if (!atSeparator) {
				result.text.replace(i, 1, separator);
			}
			digitsCount = 0;
			i += separator.size();
			continue;
		}
		result.text.remove(i, 1);
		if (result.position > i) {
			--result.position;
		}
	}
	if (result.text == "0" && result.position > 0) {
		if (was.startsWith('0')) {
			result.text = QString();
			result.position = 0;
		} else {
			result.text += separator;
			result.position += separator.size();
		}
	}
	return result;
}

} // namespace

FormattedTonAmount FormatTonAmount(int64 amount, TonFormatFlags flags) {
	auto result = FormattedTonAmount();
	const auto grams = amount / kOneTon;
	const auto preciseNanos = std::abs(amount) % kOneTon;
	auto roundedNanos = preciseNanos;
	if (flags & TonFormatFlag::Rounded) {
		if (std::abs(grams) >= 1'000'000 && (roundedNanos % 1'000'000)) {
			roundedNanos -= (roundedNanos % 1'000'000);
		} else if (std::abs(grams) >= 1'000 && (roundedNanos % 1'000)) {
			roundedNanos -= (roundedNanos % 1'000);
		}
	}
	const auto precise = (roundedNanos == preciseNanos);
	auto nanos = preciseNanos;
	auto zeros = 0;
	while (zeros < kNanoDigits && nanos % 10 == 0) {
		nanos /= 10;
		++zeros;
	}
	const auto system = QLocale::system();
	const auto locale = (flags & TonFormatFlag::Simple)
		? QLocale::c()
		: system;
	const auto separator = system.decimalPoint();

	result.wholeString = locale.toString(grams);
	if ((flags & TonFormatFlag::Signed) && amount > 0) {
		result.wholeString = locale.positiveSign() + result.wholeString;
	} else if (amount < 0 && grams == 0) {
		result.wholeString = locale.negativeSign() + result.wholeString;
	}
	result.full = result.wholeString;
	if (zeros < kNanoDigits) {
		result.separator = separator;
		result.nanoString = QString("%1"
		).arg(nanos, kNanoDigits - zeros, 10, QChar('0'));
		if (!precise) {
			const auto nanoLength = (std::abs(grams) >= 1'000'000)
				? 3
				: (std::abs(grams) >= 1'000)
				? 6
				: 9;
			result.nanoString = result.nanoString.mid(0, nanoLength);
		}
		result.full += separator + result.nanoString;
	}
	return result;
}

std::optional<int64> ParseTonAmountString(const QString &amount) {
	const auto trimmed = amount.trimmed();
	const auto separator = QString(QLocale::system().decimalPoint());
	const auto index1 = trimmed.indexOf('.');
	const auto index2 = trimmed.indexOf(',');
	const auto index3 = (separator == "." || separator == ",")
		? -1
		: trimmed.indexOf(separator);
	const auto found = (index1 >= 0 ? 1 : 0)
		+ (index2 >= 0 ? 1 : 0)
		+ (index3 >= 0 ? 1 : 0);
	if (found > 1) {
		return std::nullopt;
	}
	const auto index = (index1 >= 0)
		? index1
		: (index2 >= 0)
		? index2
		: index3;
	const auto used = (index1 >= 0)
		? "."
		: (index2 >= 0)
		? ","
		: separator;
	const auto grams = ParseAmountTons(trimmed.mid(0, index));
	const auto nano = ParseAmountNano(trimmed.mid(index + used.size()));
	if (index < 0 || index == trimmed.size() - used.size()) {
		return grams;
	} else if (index == 0) {
		return nano;
	} else if (!nano || !grams) {
		return std::nullopt;
	}
	return *grams + (*grams < 0 ? (-*nano) : (*nano));
}

QString TonAmountSeparator() {
	return FormatTonAmount(1).separator;
}

not_null<Ui::InputField*> CreateTonAmountInput(
		not_null<QWidget*> parent,
		rpl::producer<QString> placeholder,
		int64 amount) {
	const auto result = Ui::CreateChild<Ui::InputField>(
		parent.get(),
		st::editTagField,
		Ui::InputField::Mode::SingleLine,
		std::move(placeholder),
		(amount > 0
			? FormatTonAmount(amount, TonFormatFlag::Simple).full
			: QString()));
	const auto lastAmountValue = std::make_shared<QString>();
	result->changes() | rpl::start_with_next([=] {
		Ui::PostponeCall(result, [=] {
			const auto position = result->textCursor().position();
			const auto now = result->getLastText();
			const auto fixed = FixTonAmountInput(
				*lastAmountValue,
				now,
				position);
			*lastAmountValue = fixed.text;
			if (fixed.text == now) {
				return;
			}
			result->setText(fixed.text);
			result->setFocusFast();
			result->setCursorPosition(fixed.position);
		});
	}, result->lifetime());
	return result;
}

} // namespace Wallet
