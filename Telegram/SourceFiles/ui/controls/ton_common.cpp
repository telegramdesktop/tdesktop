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
#include "styles/style_chat_helpers.h"
//#include "styles/style_wallet.h"

#include <QtCore/QLocale>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>

namespace Ui {
namespace {

constexpr auto kOneTon = kNanosInOne;
constexpr auto kNanoDigits = 9;

// QLineEditPrivate::horizontalMargin: the margin a line edit keeps between
// its contents rect and its text, on both sides.
constexpr auto kLineEditMargin = 2;

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

} // namespace

FixedAmount FixTonAmountInput(
		const QString &was,
		const QString &text,
		int position,
		int fractionDigits,
		const QString &separator) {
	constexpr auto kMaxDigitsCount = 9;

	auto result = FixedAmount{ text, position };
	if (text.isEmpty()) {
		return result;
	}
	auto separatorFound = false;
	auto digitsCount = 0;
	for (auto i = 0; i != result.text.size();) {
		const auto ch = result.text[i];
		const auto atSeparator = QStringView(result.text).mid(i).startsWith(separator);
		if (ch >= '0' && ch <= '9'
			&& digitsCount < (separatorFound
				? fractionDigits
				: kMaxDigitsCount)) {
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
	const auto zero = u"0"_q;
	const auto zeroOnly = zero + separator;
	if (was == zeroOnly
		&& (result.text.isEmpty()
			|| result.text == zero
			|| result.text == separator)) {
		// Nothing is left of a canonical zero, so the whole amount is gone.
		return FixedAmount();
	} else if (result.text.startsWith(separator)) {
		if (result.text.size() > separator.size()
			&& was.startsWith(zeroOnly)
			&& was.size() > zeroOnly.size()) {
			// The zero before the separator of a fraction-only amount was
			// removed, which promotes the fraction to the whole amount.
			result.text = result.text.mid(separator.size());
			result.position = std::max(
				result.position - int(separator.size()),
				0);
		} else {
			result.text.prepend(zero);
			++result.position;
		}
	}
	const auto separatorAt = result.text.indexOf(separator);
	const auto integerLength = (separatorAt >= 0)
		? separatorAt
		: int(result.text.size());
	auto extraZeros = 0;
	while (extraZeros + 1 < integerLength
		&& result.text[extraZeros] == QChar('0')) {
		++extraZeros;
	}
	if (extraZeros > 0) {
		result.text.remove(0, extraZeros);
		result.position = std::max(result.position - extraZeros, 0);
	}
	if (result.text == zero) {
		// A zero alone is not an amount, it only starts a fractional one.
		result.text += separator;
		result.position += separator.size();
	}
	return result;
}

FormattedTonAmount FormatTonAmount(int64 amount, TonFormatFlags flags) {
	auto result = FormattedTonAmount();
	const auto grams = amount / kOneTon;
	const auto preciseNanos = std::abs(amount % kOneTon);
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

std::optional<int64> ParseTonAmountString(
		const QString &amount,
		const QString &separator) {
	const auto trimmed = amount.trimmed();
	const auto decimal = separator.isEmpty()
		? TonAmountSeparator()
		: separator;
	const auto index1 = trimmed.indexOf('.');
	const auto index2 = trimmed.indexOf(',');
	const auto index3 = (decimal == "." || decimal == ",")
		? -1
		: trimmed.indexOf(decimal);
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
		: decimal;
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
		int64 amount,
		Fn<int()> fractionDigits,
		const style::InputField *st,
		Fn<QString()> separator) {
	const auto result = Ui::CreateChild<Ui::InputField>(
		parent.get(),
		st ? *st : st::editTagField,
		Ui::InputField::Mode::SingleLine,
		std::move(placeholder),
		(amount > 0
			? FormatTonAmount(amount, TonFormatFlag::Simple).full
			: QString()));
	result->setInputMethodHints(Qt::ImhFormattedNumbersOnly
		| Qt::ImhNoPredictiveText);
	const auto lastAmountValue = std::make_shared<QString>();
	result->changes() | rpl::on_next([=] {
		Ui::PostponeCall(result, [=] {
			const auto position = result->textCursor().position();
			const auto now = result->getLastText();
			const auto fixed = FixTonAmountInput(
				*lastAmountValue,
				now,
				position,
				fractionDigits ? fractionDigits() : kNanoDigits,
				separator ? separator() : TonAmountSeparator());
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

TonAmountInput::TonAmountInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	int64 amount,
	Fn<int()> fractionDigits,
	Fn<QString()> separator)
: MaskedInputField(
	parent,
	st,
	std::move(placeholder),
	(amount > 0
		? FormatTonAmount(amount, TonFormatFlag::Simple).full
		: QString()))
, _fractionDigits(std::move(fractionDigits))
, _separator(std::move(separator)) {
	setInputMethodHints(Qt::ImhFormattedNumbersOnly
		| Qt::ImhNoPredictiveText);

	// The field is sized to its own text and placed by its owner, so it lays
	// the text out from the left: a centered line edit would put the text in
	// the middle of whatever width it happens to have, and the caret with it.
	setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	// A line edit lays its text out inside the contents rect its style gives
	// it, less the text margins, less a horizontal margin of its own. It is
	// taken from the style rather than from cursorRect(), which is padded for
	// repainting and reports five pixels left of the caret it draws.
	auto option = QStyleOptionFrame();
	initStyleOption(&option);
	const auto contents = style()->subElementRect(
		QStyle::SE_LineEditContents,
		&option,
		this);
	_textLeft = contents.x() + textMargins().left() + kLineEditMargin;

	connect(this, &MaskedInputField::changed, [=] {
		refreshNaturalWidth();
		_changes.fire({});
	});
	connect(this, &MaskedInputField::submitted, [=] {
		_submits.fire({});
	});
	refreshNaturalWidth();
}

void TonAmountInput::setText(const QString &text) {
	MaskedInputField::setText(text);
	refreshNaturalWidth();
	_changes.fire({});
}

int TonAmountInput::textWidth() const {
	return _textWidth;
}

// QLineEdit shows its text whole only while the bearings around it, the text
// itself and the caret fit inside its contents rect deflated by its own
// margin on both sides; anything narrower scrolls the text and clips it. The
// bearings are the font's widest overhangs rather than this value's, so the
// width reserved here can exceed the digits - textWidth() is what the digits
// actually span, and what a row places its neighbours by.
void TonAmountInput::refreshNaturalWidth() {
	const auto metrics = fontMetrics();
	const auto left = std::max(0, -metrics.minLeftBearing());
	const auto right = std::max(0, -metrics.minRightBearing());
	const auto caret = std::max(
		style()->pixelMetric(QStyle::PM_TextCursorWidth, nullptr, this),
		1);
	_textWidth = int(std::ceil(
		QFontMetricsF(font()).horizontalAdvance(getLastText())));
	setNaturalWidth(2 * _textLeft + left + _textWidth + caret + right);
}

rpl::producer<> TonAmountInput::changes() const {
	return _changes.events();
}

rpl::producer<> TonAmountInput::submits() const {
	return _submits.events();
}

int TonAmountInput::textLeft() const {
	return _textLeft;
}

void TonAmountInput::setExtraMargins(QMargins margins) {
	setTextMargins(_st.textMargins + margins);
}

void TonAmountInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	const auto fixed = FixTonAmountInput(
		was,
		now,
		nowCursor,
		_fractionDigits ? _fractionDigits() : kNanoDigits,
		_separator ? _separator() : TonAmountSeparator());
	setCorrectedText(now, nowCursor, fixed.text, fixed.position);
}

} // namespace Wallet
