/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_amount_field.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/controls/ton_common.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "wallet/wallet_amount_painter.h"

#include <QtCore/QLocale>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QStyleHints>
#include <QtWidgets/QStyle>

#include "styles/palette.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

struct AmountLayout {
	std::vector<float64> caret;
	std::vector<float64> right;
	int separatorAt = -1;
};

class AmountRow final : public Ui::RpWidget {
public:
	AmountRow(QWidget *parent, AmountFieldArgs &args);

	[[nodiscard]] not_null<Ui::TonAmountInput*> field() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void refreshContent();
	void refreshGeometry();
	void restartBlink();
	void paintAddition(QPainter &p) const;
	void select(int anchor, int position);

	[[nodiscard]] QRect caretRect() const;
	[[nodiscard]] std::array<QRectF, 2> selectionRects() const;
	[[nodiscard]] int positionAt(int x) const;
	[[nodiscard]] bool inDigitsZone(int x) const;
	[[nodiscard]] int selectionAnchor() const;

	const not_null<Ui::TonAmountInput*> _field;
	const Fn<QString()> _separator;
	const int _diamondCanvas = 0;
	const int _figureBig = 0;
	const int _figureSmall = 0;
	std::unique_ptr<Lottie::Icon> _diamond;
	AmountPainter _painter;
	AmountLayout _layout;
	QString _currency;
	QString _symbol;
	QString _ticker;
	base::Timer _blink;
	float64 _left = 0.;
	float64 _top = 0.;
	int _anchor = -1;
	bool _fiat = false;
	bool _caretShown = false;
	bool _selecting = false;

};

[[nodiscard]] int AmountBand() {
	const auto &st = st::walletSendUserAmountField;
	return std::max(st.heightMin, st.style.font->height);
}

[[nodiscard]] int FigureHeight(const style::font &font) {
	return int(base::SafeRound(
		-font->metrics().tightBoundingRect(u"0123456789"_q).top()));
}

[[nodiscard]] int DiamondPart(int canvas, float64 part) {
	return int(base::SafeRound(canvas * part));
}

[[nodiscard]] int CountDigits(const QString &text) {
	return int(ranges::count_if(text, [](QChar ch) { return ch.isDigit(); }));
}

[[nodiscard]] QString GroupWhole(
		const QString &digits,
		bool fiat,
		const QString &currency) {
	auto result = QString();
	if (digits.isEmpty()) {
		return result;
	} else if (!fiat) {
		result = QLocale::system().toString(qlonglong(digits.toLongLong()));
	} else if (const auto thousands
			= Ui::LookupCurrencyRule(currency).thousands) {
		const auto size = int(digits.size());
		result.reserve(size + size / 3);
		for (auto i = 0; i != size; ++i) {
			if (i > 0 && (size - i) % 3 == 0) {
				result.append(QChar(thousands));
			}
			result.append(digits[i]);
		}
	}
	return (CountDigits(result) == digits.size()) ? result : digits;
}

[[nodiscard]] AmountLayout ComputeAmountLayout(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small,
		int wholeLeft,
		int fractionLeft) {
	const auto digits = CountDigits(whole);
	const auto length = digits + int(fraction.size());
	auto result = AmountLayout{
		.separatorAt = fraction.isEmpty() ? -1 : digits,
	};
	result.caret.reserve(length + 1);
	result.right.reserve(length);
	const auto &bigMetrics = big->metrics();
	for (auto i = 0; i != int(whole.size()); ++i) {
		if (!whole[i].isDigit()) {
			continue;
		}
		result.caret.push_back(
			wholeLeft + bigMetrics.horizontalAdvance(whole.left(i)));
		result.right.push_back(
			wholeLeft + bigMetrics.horizontalAdvance(whole.left(i + 1)));
	}
	result.caret.push_back(fractionLeft);
	const auto &smallMetrics = small->metrics();
	for (auto i = 0; i != int(fraction.size()); ++i) {
		const auto right = fractionLeft
			+ smallMetrics.horizontalAdvance(fraction.left(i + 1));
		result.right.push_back(right);
		result.caret.push_back(right);
	}
	return result;
}

AmountRow::AmountRow(QWidget *parent, AmountFieldArgs &args)
: RpWidget(parent)
, _field(Ui::CreateChild<Ui::TonAmountInput>(
	this,
	st::walletSendUserAmountField,
	args.value,
	std::move(args.fractionDigits),
	args.separator))
, _separator(std::move(args.separator))
, _diamondCanvas(
	GramDiamondCanvas(st::walletSendUserAmountField.style.font))
, _figureBig(FigureHeight(st::walletSendUserAmountField.style.font))
, _figureSmall(FigureHeight(st::walletSendUserTickerLabel.style.font))
// Not CreateLottieIcon: it replays any icon resting past its first frame.
, _diamond(Lottie::MakeIcon({
	.name = u"gram"_q,
	.sizeOverride = { _diamondCanvas, _diamondCanvas },
	.frame = -1,
	.limitFps = true,
}))
, _blink([=] {
	_caretShown = !_caretShown;
	update(caretRect());
}) {
	_field->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	setCursor(style::cur_pointer);

	rpl::combine(
		std::move(args.entryFiat),
		std::move(args.currency),
		tr::lng_wallet_card_ticker()
	) | rpl::on_next([=](bool fiat, QString code, QString gram) {
		const auto name = Ui::CurrencyName(code);
		_fiat = fiat;
		_ticker = fiat ? code : gram;
		_symbol = (fiat && name != code) ? name : QString();
		_currency = std::move(code);
		refreshContent();
	}, lifetime());

	_field->changes() | rpl::on_next([=] {
		refreshContent();
	}, lifetime());
	const auto moved = [=] {
		restartBlink();
		update();
		if (_field->hasFocus()) {
			QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
		}
	};
	connect(_field, &QLineEdit::cursorPositionChanged, this, moved);
	connect(_field, &QLineEdit::selectionChanged, this, moved);
	connect(_field, &Ui::MaskedInputField::focused, this, [=] {
		restartBlink();
	});
	connect(_field, &Ui::MaskedInputField::blurred, this, [=] {
		restartBlink();
	});
	_field->setCaretRectCallback([=] {
		return caretRect().translated(-_field->pos());
	});

	sizeValue() | rpl::on_next([=] {
		refreshGeometry();
	}, lifetime());
	refreshContent();
}

not_null<Ui::TonAmountInput*> AmountRow::field() const {
	return _field;
}

void AmountRow::refreshContent() {
	const auto &text = _field->getLastText();
	const auto separator = _separator ? _separator() : QString();
	const auto at = separator.isEmpty() ? -1 : int(text.indexOf(separator));
	const auto digits = (at >= 0) ? text.left(at) : text;
	const auto fraction = (at >= 0) ? text.mid(at) : QString();
	const auto whole = GroupWhole(digits, _fiat, _currency);
	const auto &big = st::walletSendUserAmountField.style.font;
	const auto &small = st::walletSendUserTickerLabel.style.font;
	const auto additionWidth = !_fiat
		? DiamondPart(_diamondCanvas, kGramDiamondRight - kGramDiamondLeft)
		: _symbol.isEmpty()
		? 0
		: st::walletSendUserAmountLabel.style.font->width(_symbol);
	const auto gap = st::walletDetailsAmountMinorSkip;
	_painter.setContent({
		.big = big,
		.small = small,
		.ticker = small,
		.additionWidth = additionWidth,
		.additionSkip = gap,
		.tickerSkip = gap,
	}, {
		.whole = whole,
		.fraction = fraction,
		.ticker = _ticker,
	});
	_layout = ComputeAmountLayout(
		whole,
		fraction,
		big,
		small,
		_painter.wholeLeft(),
		_painter.fractionLeft());
	refreshGeometry();
}

void AmountRow::refreshGeometry() {
	_painter.setAvailableWidth(width());
	const auto k = _painter.scale();
	_left = std::floor((width() - k * _painter.naturalWidth()) / 2.);
	_top = std::floor((height() - k * _painter.naturalHeight()) / 2.);
	const auto gap = st::walletDetailsAmountMinorSkip;
	const auto wholeLeft = _painter.wholeLeft();
	const auto digitsWidth = _painter.tickerLeft() - gap - wholeLeft;
	_field->setGeometry(
		int(std::floor(_left + k * wholeLeft)),
		0,
		std::max(int(std::ceil(k * digitsWidth)), 1),
		height());
	update();
}

void AmountRow::restartBlink() {
	_caretShown = _field->hasFocus();
	const auto interval = QGuiApplication::styleHints()->cursorFlashTime() / 2;
	if (_caretShown && interval > 0) {
		_blink.callEach(interval);
	} else {
		_blink.cancel();
	}
	update(caretRect());
}

QRect AmountRow::caretRect() const {
	if (_layout.caret.empty()) {
		return QRect();
	}
	const auto position = std::clamp(
		_field->cursorPosition(),
		0,
		int(_layout.caret.size()) - 1);
	const auto whole = (_layout.separatorAt < 0)
		|| (position <= _layout.separatorAt);
	const auto figure = whole ? _figureBig : _figureSmall;
	const auto k = _painter.scale();
	const auto baseline = _painter.baseline();
	const auto left = base::SafeRound(_left + k * _layout.caret[position]);
	const auto top = base::SafeRound(_top + k * (baseline - figure));
	const auto bottom = base::SafeRound(_top + k * baseline);
	const auto width = std::max(
		_field->style()->pixelMetric(
			QStyle::PM_TextCursorWidth,
			nullptr,
			_field),
		1);
	return QRect(int(left), int(top), width, int(bottom - top));
}

std::array<QRectF, 2> AmountRow::selectionRects() const {
	auto result = std::array<QRectF, 2>();
	if (!_field->hasSelectedText()) {
		return result;
	}
	const auto length = int(_layout.right.size());
	const auto from = std::clamp(_field->selectionStart(), 0, length);
	const auto till = std::clamp(
		from + _field->selectionLength(),
		from,
		length);
	const auto whole = (_layout.separatorAt >= 0)
		? _layout.separatorAt
		: length;
	const auto baseline = _painter.baseline();
	if (from < whole && from < till) {
		const auto end = std::min(till, whole);
		const auto left = _layout.caret[from];
		const auto right = (end == whole)
			? _layout.caret[whole]
			: _layout.right[end - 1];
		result[0] = QRectF(
			left,
			baseline - _figureBig,
			right - left,
			_figureBig);
	}
	if (till > whole) {
		const auto start = std::max(from, whole);
		const auto left = _layout.caret[start];
		const auto right = _layout.right[till - 1];
		result[1] = QRectF(
			left,
			baseline - _figureSmall,
			right - left,
			_figureSmall);
	}
	return result;
}

int AmountRow::positionAt(int x) const {
	const auto natural = (x - _left) / _painter.scale();
	auto result = 0;
	auto distance = std::numeric_limits<float64>::max();
	for (auto i = 0; i != int(_layout.caret.size()); ++i) {
		const auto now = std::abs(_layout.caret[i] - natural);
		if (now < distance) {
			distance = now;
			result = i;
		}
	}
	return result;
}

bool AmountRow::inDigitsZone(int x) const {
	const auto k = _painter.scale();
	const auto half = st::walletDetailsAmountMinorSkip / 2.;
	const auto from = _left + k * (_painter.wholeLeft() - half);
	const auto till = _left + k * (_painter.tickerLeft() - half);
	return (x >= from) && (x <= till);
}

int AmountRow::selectionAnchor() const {
	const auto cursor = _field->cursorPosition();
	if (!_field->hasSelectedText()) {
		return cursor;
	}
	const auto start = _field->selectionStart();
	const auto end = start + _field->selectionLength();
	return (cursor == start) ? end : start;
}

void AmountRow::select(int anchor, int position) {
	if (anchor == position) {
		_field->setCursorPosition(position);
	} else {
		_field->setSelection(anchor, position - anchor);
	}
}

void AmountRow::paintAddition(QPainter &p) const {
	const auto baseline = _painter.baseline();
	if (!_fiat) {
		_diamond->paint(
			p,
			-DiamondPart(_diamondCanvas, kGramDiamondLeft),
			baseline - DiamondPart(_diamondCanvas, kGramDiamondBottom));
	} else if (!_symbol.isEmpty()) {
		const auto &st = st::walletSendUserAmountLabel;
		p.setFont(st.style.font);
		p.setPen(st.textFg);
		p.drawText(QPointF(0., baseline), _symbol);
	}
}

void AmountRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto k = _painter.scale();
	const auto &fg = st::walletSendUserAmountField.textFg;
	const auto ticker = st::walletSendUserTickerLabel.textFg->c;
	p.translate(_left, _top);
	p.scale(k, k);
	paintAddition(p);
	const auto selection = selectionRects();
	for (const auto &rect : selection) {
		if (!rect.isEmpty()) {
			p.fillRect(rect, st::msgInBgSelected);
		}
	}
	_painter.paint(p, { .digits = fg->c, .ticker = ticker });
	for (const auto &rect : selection) {
		if (!rect.isEmpty()) {
			p.save();
			p.setClipRect(rect);
			_painter.paint(p, {
				.digits = st::historyTextInFgSelected->c,
				.ticker = ticker,
			});
			p.restore();
		}
	}
	p.resetTransform();
	if (_caretShown && _field->hasFocus()) {
		p.fillRect(caretRect(), fg);
	}
}

void AmountRow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_field->setFocusFast();
	const auto x = e->pos().x();
	if (!inDigitsZone(x)) {
		return;
	}
	const auto position = positionAt(x);
	if (e->modifiers() & Qt::ShiftModifier) {
		_anchor = selectionAnchor();
		select(_anchor, position);
	} else {
		_anchor = position;
		_field->setCursorPosition(position);
	}
	_selecting = true;
}

void AmountRow::mouseMoveEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	const auto shape = inDigitsZone(x)
		? style::cur_text
		: style::cur_pointer;
	if (cursor().shape() != shape) {
		setCursor(shape);
	}
	if (_selecting && (e->buttons() & Qt::LeftButton)) {
		select(_anchor, positionAt(x));
	}
}

void AmountRow::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_selecting = false;
	}
}

void AmountRow::mouseDoubleClickEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_field->setFocusFast();
	_selecting = false;
	if (inDigitsZone(e->pos().x())) {
		_field->selectAll();
	}
}

void AmountRow::contextMenuEvent(QContextMenuEvent *e) {
	_field->setFocusFast();
	auto mapped = QContextMenuEvent(
		e->reason(),
		_field->mapFrom(this, e->pos()),
		e->globalPos(),
		e->modifiers());
	QCoreApplication::sendEvent(_field, &mapped);
	e->accept();
}

} // namespace

not_null<Ui::TonAmountInput*> AddAmountField(
		not_null<Ui::VerticalLayout*> container,
		int topSkip,
		AmountFieldArgs &&args) {
	const auto wrap = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(
			st::walletSendFieldMargin.left(),
			topSkip,
			st::walletSendFieldMargin.right(),
			st::walletSendFieldMargin.bottom()));
	const auto row = Ui::CreateChild<AmountRow>(wrap, args);
	const auto pill = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		rpl::single(QString()),
		st::walletSendUserFiatButton);
	pill->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	pill->setContext(args.equivalentContext);
	pill->setText(std::move(
		args.equivalent
	) | rpl::map([](TextWithEntities text) {
		return text.append(u" ↑↓"_q);
	}));
	pill->setClickedCallback(std::move(args.swap));
	std::move(args.equivalentShown) | rpl::on_next([=](bool shown) {
		pill->setVisible(shown);
	}, pill->lifetime());
	const auto band = AmountBand();
	rpl::combine(
		wrap->widthValue(),
		pill->naturalWidthValue()
	) | rpl::on_next([=](int width, int) {
		row->setGeometry(0, 0, width, band);
		pill->resize(std::min(pill->naturalWidth(), width), pill->height());
		pill->moveToLeft(
			(width - pill->width()) / 2,
			band + st::walletSendFieldMargin.top(),
			width);
		wrap->resize(width, pill->y() + pill->height());
	}, wrap->lifetime());
	return row->field();
}

} // namespace Wallet
