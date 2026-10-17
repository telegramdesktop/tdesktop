/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "ui/widgets/fields/masked_input_field.h"

namespace style {
struct InputField;
} // namespace style

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
	const QString &amount,
	const QString &separator = QString());

[[nodiscard]] QString TonAmountSeparator();

struct FixedAmount {
	QString text;
	int position = 0;
};

// Turns whatever was typed into the one canonical form of an amount. |was| is
// the previous canonical text, which is what tells a removal from an entry:
// backspacing the zero out of "0<separator>" clears the field, while typing a
// separator into an empty one starts "0<separator>".
[[nodiscard]] FixedAmount FixTonAmountInput(
	const QString &was,
	const QString &text,
	int position,
	int fractionDigits,
	const QString &separator);

// FixTonAmountInput for an amount that never empties below "0".
[[nodiscard]] FixedAmount FixTonAmountValue(
	const QString &was,
	int wasCursor,
	const QString &text,
	int position,
	int fractionDigits,
	const QString &separator);

// An amount entry that paints nothing: its owner paints what it holds.
class TonAmountInput final : public MaskedInputField {
public:
	TonAmountInput(
		QWidget *parent,
		const style::InputField &st,
		int64 amount,
		Fn<int()> fractionDigits,
		Fn<QString()> separator);

	// Hides the non-virtual base setText: a programmatic value is trusted as
	// it is, and it reports itself through changes() like an edit does.
	void setText(const QString &text);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<> submits() const;
	[[nodiscard]] const style::InputField &st() const {
		return _st;
	}

	void setCaretRectCallback(Fn<QRect()> callback);
	// The input method's preedit, kept outside the text until a commit.
	[[nodiscard]] const QString &composition() const;
	[[nodiscard]] rpl::producer<> compositionChanges() const;
	void commitComposition();

	QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	void setComposition(const QString &text);
	void insertTyped(const QString &text);

	const Fn<int()> _fractionDigits;
	const Fn<QString()> _separator;
	Fn<QRect()> _caretRect;
	QString _composition;
	rpl::event_stream<> _changes;
	rpl::event_stream<> _submits;
	rpl::event_stream<> _compositionChanges;

};

[[nodiscard]] not_null<Ui::InputField*> CreateTonAmountInput(
	not_null<QWidget*> parent,
	rpl::producer<QString> placeholder,
	int64 amount = 0,
	Fn<int()> fractionDigits = nullptr,
	const style::InputField *st = nullptr,
	Fn<QString()> separator = nullptr);

} // namespace Ui
