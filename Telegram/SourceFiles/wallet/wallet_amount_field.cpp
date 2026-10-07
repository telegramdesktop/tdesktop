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
#include "ui/effects/animations.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_amount_painter.h"

#include <QtCore/QLocale>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QStyleHints>
#include <QtGui/QTextLayout>
#include <QtWidgets/QStyle>

#include "styles/palette.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kEditDuration = crl::time(180);
constexpr auto kSwitchFirstDuration = crl::time(300);
constexpr auto kSwitchDuration = crl::time(450);
constexpr auto kSwitchFadeDuration = crl::time(300);

enum class Ease {
	OutCubic,
	OutCirc,
};

enum class EditKind {
	None,
	Replace,
	Append,
	Remove,
	Immediate,
};

enum class GlyphKind {
	Digit,
	Decimal,
	Group,
	Other,
};

enum class GlyphMode {
	Scale,
	Fade,
	Roll,
};

enum class LayerKind {
	Diamond,
	Symbol,
	Ticker,
};

struct Motion {
	[[nodiscard]] float64 value(crl::time now) const;
	[[nodiscard]] bool running(crl::time now) const;
	void retarget(
		float64 target,
		crl::time now,
		crl::time length,
		Ease kind = Ease::OutCubic);
	void jump(float64 target);

	float64 from = 0.;
	float64 to = 0.;
	crl::time start = 0;
	crl::time duration = 0;
	Ease ease = Ease::OutCubic;
};

struct GlyphTarget {
	QChar ch;
	GlyphKind kind = GlyphKind::Digit;
	bool small = false;
	float64 width = 0.;
	int place = 0;

	friend inline bool operator==(
		const GlyphTarget &,
		const GlyphTarget &) = default;
};

struct GlyphLayer {
	QChar ch;
	GlyphMode mode = GlyphMode::Scale;
	int side = 0;
	float64 width = 0.;
	Motion v;
};

struct Slot {
	uint32 id = 0;
	GlyphKind kind = GlyphKind::Digit;
	bool small = false;
	bool dying = false;
	int place = 0;
	Motion width;
	Motion presence;
	GlyphLayer current;
	std::vector<GlyphLayer> leaving;
};

struct Separator {
	QChar ch;
	uint32 gap = 0;
	float64 width = 0.;
	float64 fromX = 0.;
	Motion slide;
	Motion presence;
};

struct GlyphChange {
	GlyphMode mode = GlyphMode::Scale;
	int side = 0;
	Ease ease = Ease::OutCubic;
	bool slide = false;
	std::vector<crl::time> durations;
};

struct InkRange {
	float64 left = 0.;
	float64 right = 0.;
};

struct FlowGap {
	int position = -1;
	float64 width = 0.;
};

struct Layer {
	LayerKind kind = LayerKind::Ticker;
	QString text;
	float64 width = 0.;
	bool fiat = false;
	Motion v;
};

struct LabelPart {
	TextWithEntities source;
	Ui::Text::String text;
	float64 lead = 0.;
	float64 width = 0.;
	Motion v;
};

struct RowGeometry {
	float64 k = 1.;
	float64 left = 0.;
	float64 top = 0.;
	float64 additions = 0.;
	float64 flow = 0.;
	float64 composition = 0.;
	float64 tickerSkip = 0.;
};

struct AmountLayout {
	std::vector<float64> caret;
	std::vector<float64> right;
	int separatorAt = -1;
};

class GlyphFlow final {
public:
	GlyphFlow(const style::font &big, const style::font &small);

	void reset(std::vector<GlyphTarget> targets);
	void edit(
		std::vector<GlyphTarget> targets,
		EditKind kind,
		crl::time now);
	void roll(
		std::vector<GlyphTarget> targets,
		GlyphMode mode,
		bool growing,
		crl::time now);
	void prune(crl::time now);
	void finish();

	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] float64 width(crl::time now) const;
	[[nodiscard]] float64 caretX(int position, crl::time now) const;

	void paint(
		QPainter &p,
		QPointF origin,
		float64 baseline,
		const QColor &color,
		crl::time now,
		FlowGap gap = FlowGap()) const;

private:
	struct Shape {
		QPainterPath path;
		QRectF ink;
	};

	void apply(
		std::vector<GlyphTarget> targets,
		const std::vector<int> &match,
		const GlyphChange &change,
		crl::time now);
	void applySeparators(
		std::vector<float64> xs,
		std::vector<uint32> killed,
		std::vector<uint32> born,
		const GlyphChange &change,
		crl::time now);
	void changeGlyph(
		Slot &slot,
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) const;
	void paintLayer(
		QPainter &p,
		const Slot &slot,
		const GlyphLayer &layer,
		QPointF centre,
		float64 presence,
		const QColor &color,
		crl::time now) const;

	[[nodiscard]] Slot createSlot(
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now);
	[[nodiscard]] Slot settledSlot(const GlyphTarget &target);
	[[nodiscard]] Separator settledSeparator(const Slot &gap) const;
	[[nodiscard]] std::vector<float64> lefts(crl::time now) const;
	[[nodiscard]] std::vector<float64> restLefts() const;
	[[nodiscard]] int slotIndex(uint32 id) const;
	[[nodiscard]] int gapIndex(int position) const;
	[[nodiscard]] float64 separatorX(
		const Separator &separator,
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] float64 dip(
		const Separator &separator,
		float64 x,
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] std::vector<InkRange> visibleInks(
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] std::vector<InkRange> restInks(
		const std::vector<float64> &lefts) const;
	[[nodiscard]] Shape shape(bool small, QChar ch) const;

	const not_null<const style::font*> _big;
	const not_null<const style::font*> _small;
	const QRectF _digitsInk;
	std::vector<Slot> _slots;
	std::vector<Separator> _separators;
	std::vector<GlyphTarget> _targets;
	mutable base::flat_map<std::pair<bool, QChar>, Shape> _shapes;
	uint32 _autoincrement = 0;

};

class AmountRow final : public Ui::RpWidget {
public:
	AmountRow(QWidget *parent, AmountFieldArgs &args);

	[[nodiscard]] not_null<Ui::TonAmountInput*> field() const;
	[[nodiscard]] AmountDiamond takeDiamond();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void refreshContent();
	void refreshLayers(bool switching, crl::time now);
	void refreshGeometry();
	void applyChange(
		const QString &text,
		const QString &separator,
		const QString &whole,
		const QString &fraction,
		bool switching);
	void startAnimation();
	void finishContent();
	void finishAnimation();
	void playDiamond();
	void restartBlink();
	void paintAddition(QPainter &p, float64 allotted, crl::time now) const;
	void paintTickers(QPainter &p, float64 left, crl::time now) const;
	void paintComposition(QPainter &p, float64 left, float64 baseline) const;
	void paintAnimated(QPainter &p, crl::time now) const;
	void select(int anchor, int position);

	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] bool contentAnimating(crl::time now) const;
	[[nodiscard]] bool flowPainted() const;
	[[nodiscard]] RowGeometry animatedGeometry(crl::time now) const;
	[[nodiscard]] QRect additionRect() const;
	[[nodiscard]] std::optional<QRectF> diamondCanvas(crl::time now) const;
	[[nodiscard]] QRect caretRect() const;
	[[nodiscard]] QRect caretRect(
		float64 k,
		float64 left,
		float64 top,
		float64 x) const;
	[[nodiscard]] QRect flowCaretRect(
		const RowGeometry &geometry,
		crl::time now) const;
	[[nodiscard]] int caretPosition() const;
	[[nodiscard]] bool caretInFraction() const;
	[[nodiscard]] const style::font &compositionFont() const;
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
	GlyphFlow _flow;
	std::vector<Layer> _additions;
	std::vector<Layer> _tickers;
	Ui::Animations::Basic _animation;
	AmountPainter _painter;
	AmountLayout _layout;
	QString _shownText;
	QString _shownSeparator;
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
	bool _switchPending = false;
	bool _initialized = false;

};

class EquivalentLabel final : public Ui::RpWidget {
public:
	EquivalentLabel(QWidget *parent, Ui::Text::MarkedContext context);

	void setLabel(AmountLabel label);
	[[nodiscard]] rpl::producer<int> labelWidthValue() const;
	[[nodiscard]] QString plainText() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void crossFade(
		std::vector<LabelPart> &parts,
		const TextWithEntities &source,
		crl::time duration,
		crl::time now);
	void finishContent();
	void refreshWidth(crl::time now);
	void paintAnimated(QPainter &p, crl::time now) const;
	float64 paintParts(
		QPainter &p,
		std::span<const LabelPart> parts,
		float64 left,
		crl::time now) const;

	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] LabelPart createPart(const TextWithEntities &source) const;
	[[nodiscard]] float64 restWidth() const;
	[[nodiscard]] float64 animatedWidth(crl::time now) const;

	Ui::Text::MarkedContext _context;
	Ui::Text::String _static;
	GlyphFlow _flow;
	std::vector<LabelPart> _prefixes;
	std::vector<LabelPart> _suffixes;
	LabelPart _arrows;
	std::vector<GlyphTarget> _targets;
	AmountLabel _label;
	Motion _extra;
	Ui::Animations::Basic _animation;
	rpl::variable<int> _width = 0;
	bool _initialized = false;

};

class SwapPill final : public Ui::RoundButton {
public:
	using RoundButton::RoundButton;

	void setAccessibleText(QString text);
	QString accessibilityName() override;

private:
	QString _accessibleText;

};

[[nodiscard]] float64 Eased(Ease ease, float64 t) {
	return (ease == Ease::OutCirc)
		? anim::easeOutCirc(1., t)
		: anim::easeOutCubic(1., t);
}

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

[[nodiscard]] std::vector<float64> GlyphLefts(
		const style::font &font,
		const QString &text) {
	auto result = std::vector<float64>();
	if (text.isEmpty()) {
		return result;
	}
	auto layout = QTextLayout(text, font->f);
	layout.beginLayout();
	auto line = layout.createLine();
	line.setLineWidth(std::numeric_limits<short>::max());
	layout.endLayout();
	result.reserve(text.size());
	for (auto i = 0; i != int(text.size()); ++i) {
		result.push_back(line.cursorToX(i));
	}
	return result;
}

[[nodiscard]] std::vector<GlyphTarget> AmountTargets(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small) {
	auto result = std::vector<GlyphTarget>();
	result.reserve(whole.size() + fraction.size());
	const auto wholeLefts = GlyphLefts(big, whole);
	const auto wholeSize = int(whole.size());
	auto digitsAfter = CountDigits(whole);
	for (auto i = 0; i != wholeSize; ++i) {
		const auto ch = whole[i];
		const auto digit = ch.isDigit();
		if (digit) {
			--digitsAfter;
		}
		const auto right = (i + 1 < wholeSize)
			? wholeLefts[i + 1]
			: float64(big->width(whole));
		result.push_back({
			.ch = ch,
			.kind = digit ? GlyphKind::Digit : GlyphKind::Group,
			.width = right - wholeLefts[i],
			.place = digit ? digitsAfter : (digitsAfter - 1),
		});
	}
	const auto fractionLefts = GlyphLefts(small, fraction);
	const auto fractionSize = int(fraction.size());
	for (auto i = 0; i != fractionSize; ++i) {
		const auto ch = fraction[i];
		const auto right = (i + 1 < fractionSize)
			? fractionLefts[i + 1]
			: float64(small->width(fraction));
		result.push_back({
			.ch = ch,
			.kind = (i == 0)
				? GlyphKind::Decimal
				: ch.isDigit()
				? GlyphKind::Digit
				: GlyphKind::Other,
			.small = true,
			.width = right - fractionLefts[i],
			.place = -i,
		});
	}
	return result;
}

[[nodiscard]] std::vector<GlyphTarget> LabelTargets(
		const AmountLabel &label) {
	const auto &font = st::walletSendUserFiatButton.style.font;
	const auto at = label.decimal.isEmpty()
		? -1
		: int(label.amount.indexOf(label.decimal));
	return AmountTargets(
		(at >= 0) ? label.amount.left(at) : label.amount,
		(at >= 0) ? label.amount.mid(at) : QString(),
		font,
		font);
}

[[nodiscard]] float64 AmountValue(
		const QString &text,
		const QString &decimal) {
	const auto at = decimal.isEmpty() ? -1 : int(text.indexOf(decimal));
	auto digits = QString();
	for (auto i = 0; i != int(text.size()); ++i) {
		if (i == at) {
			digits.append(QChar('.'));
		} else if (text[i].isDigit()) {
			digits.append(text[i]);
		}
	}
	return digits.toDouble();
}

[[nodiscard]] EditKind ClassifyEdit(
		const QString &was,
		const QString &now,
		int cursor,
		const QString &separator) {
	const auto zero = u"0"_q;
	const auto nonZeroDigit = [](const QString &text) {
		return (text.size() == 1)
			&& text[0].isDigit()
			&& (text[0] != QChar('0'));
	};
	const auto wasAt = separator.isEmpty() ? -1 : int(was.indexOf(separator));
	const auto nowAt = separator.isEmpty() ? -1 : int(now.indexOf(separator));
	const auto atEnd = [&](const QString &shorter, const QString &longer) {
		return (longer.size() == shorter.size() + 1)
			&& longer.startsWith(shorter)
			&& (cursor == now.size());
	};
	const auto atWholeEnd = [&](
			const QString &shorter,
			int shorterAt,
			const QString &longer,
			int longerAt) {
		return (shorterAt >= 0)
			&& (longerAt == shorterAt + 1)
			&& (cursor == nowAt)
			&& (longer.size() == shorter.size() + 1)
			&& longer[shorterAt].isDigit()
			&& longer.startsWith(shorter.left(shorterAt))
			&& longer.endsWith(shorter.mid(shorterAt));
	};
	if (was == now) {
		return EditKind::None;
	} else if ((was == zero && nonZeroDigit(now))
		|| (now == zero && nonZeroDigit(was))) {
		return EditKind::Replace;
	} else if (atEnd(was, now) || atWholeEnd(was, wasAt, now, nowAt)) {
		return EditKind::Append;
	} else if (atEnd(now, was) || atWholeEnd(now, nowAt, was, wasAt)) {
		return EditKind::Remove;
	}
	return EditKind::Immediate;
}

[[nodiscard]] QString WholeText(const std::vector<GlyphTarget> &targets) {
	auto result = QString();
	for (const auto &target : targets) {
		if (!target.small) {
			result.append(target.ch);
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> IndicesOf(
		const std::vector<GlyphTarget> &targets,
		bool groups) {
	auto result = std::vector<int>();
	for (auto i = 0; i != int(targets.size()); ++i) {
		if ((targets[i].kind == GlyphKind::Group) == groups) {
			result.push_back(i);
		}
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<int>> EditMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now,
		EditKind kind) {
	auto result = std::vector<int>(now.size(), -1);
	const auto wasChars = IndicesOf(was, false);
	const auto nowChars = IndicesOf(now, false);
	const auto wasSize = int(wasChars.size());
	const auto nowSize = int(nowChars.size());
	if (kind == EditKind::Replace) {
		if (wasSize != 1 || nowSize != 1) {
			return std::nullopt;
		}
		result[nowChars[0]] = wasChars[0];
	} else {
		const auto grows = (kind == EditKind::Append);
		if (nowSize != wasSize + (grows ? 1 : -1)) {
			return std::nullopt;
		}
		auto prefix = 0;
		while (prefix < std::min(wasSize, nowSize)
			&& was[wasChars[prefix]].ch == now[nowChars[prefix]].ch) {
			++prefix;
		}
		for (auto i = 0; i != nowSize; ++i) {
			const auto from = (i < prefix)
				? i
				: grows
				? ((i == prefix) ? -1 : (i - 1))
				: (i + 1);
			if (from >= 0) {
				result[nowChars[i]] = wasChars[from];
			}
		}
	}
	if (WholeText(was) == WholeText(now)) {
		const auto wasGroups = IndicesOf(was, true);
		const auto nowGroups = IndicesOf(now, true);
		for (auto i = 0; i != int(nowGroups.size()); ++i) {
			result[nowGroups[i]] = wasGroups[i];
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> PlaceMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now) {
	const auto key = [](const GlyphTarget &target) {
		const auto space = (target.kind == GlyphKind::Group)
			? 2
			: (target.kind == GlyphKind::Decimal)
			? 1
			: 0;
		return std::make_pair(space, target.place);
	};
	auto result = std::vector<int>(now.size(), -1);
	auto next = 0;
	for (auto j = 0; j != int(now.size()); ++j) {
		for (auto i = next; i != int(was.size()); ++i) {
			if (key(was[i]) == key(now[j])) {
				result[j] = i;
				next = i + 1;
				break;
			}
		}
	}
	return result;
}

[[nodiscard]] float64 Reach(
		InkRange range,
		const std::vector<InkRange> &inks) {
	auto result = std::numeric_limits<float64>::max();
	for (const auto &ink : inks) {
		result = std::min(
			result,
			std::max(ink.left - range.right, range.left - ink.right));
	}
	return result;
}

[[nodiscard]] const style::color &LayerColor(const Layer &layer) {
	return layer.fiat ? st::walletSendUserFiatFg : st::walletSendUserGramFg;
}

[[nodiscard]] float64 TargetWidth(const std::vector<Layer> &layers) {
	const auto i = ranges::find(layers, 1., [](const Layer &layer) {
		return layer.v.to;
	});
	return (i != end(layers)) ? i->width : 0.;
}

void ChangeLayers(
		std::vector<Layer> &layers,
		std::optional<Layer> target,
		bool animated,
		crl::time now) {
	const auto same = [&](const Layer &layer) {
		return target
			&& (layer.kind == target->kind)
			&& (layer.text == target->text)
			&& (layer.fiat == target->fiat);
	};
	if (!animated) {
		const auto shown = ranges::find(layers, 1., [](const Layer &layer) {
			return layer.v.to;
		});
		if ((shown != end(layers)) ? same(*shown) : !target) {
			return;
		}
		layers.clear();
		if (target) {
			target->v.jump(1.);
			layers.push_back(std::move(*target));
		}
		return;
	}
	auto found = false;
	for (auto &layer : layers) {
		const auto mine = !found && same(layer);
		found = found || mine;
		layer.v.retarget(mine ? 1. : 0., now, kSwitchFadeDuration);
	}
	if (!found && target) {
		target->v.jump(0.);
		target->v.retarget(1., now, kSwitchFadeDuration);
		layers.push_back(std::move(*target));
	}
}

float64 Motion::value(crl::time now) const {
	if (duration <= 0 || now >= start + duration) {
		return to;
	} else if (now <= start) {
		return from;
	}
	const auto t = (now - start) / float64(duration);
	return from + (to - from) * Eased(ease, t);
}

bool Motion::running(crl::time now) const {
	return (duration > 0) && (now < start + duration);
}

void Motion::retarget(
		float64 target,
		crl::time now,
		crl::time length,
		Ease kind) {
	if (target == to) {
		return;
	}
	from = value(now);
	to = target;
	start = now;
	duration = length;
	ease = kind;
}

void Motion::jump(float64 target) {
	from = to = target;
	duration = 0;
}

GlyphFlow::GlyphFlow(const style::font &big, const style::font &small)
: _big(&big)
, _small(&small)
, _digitsInk(big->metrics().tightBoundingRect(u"0123456789"_q)) {
}

void GlyphFlow::reset(std::vector<GlyphTarget> targets) {
	_slots.clear();
	_separators.clear();
	_slots.reserve(targets.size());
	for (const auto &target : targets) {
		_slots.push_back(settledSlot(target));
		if (target.kind == GlyphKind::Group) {
			_separators.push_back(settledSeparator(_slots.back()));
		}
	}
	_targets = std::move(targets);
}

void GlyphFlow::edit(
		std::vector<GlyphTarget> targets,
		EditKind kind,
		crl::time now) {
	if (kind == EditKind::None && targets == _targets) {
		return;
	}
	const auto match = (kind == EditKind::None
		|| kind == EditKind::Immediate)
		? std::nullopt
		: EditMatch(_targets, targets, kind);
	if (!match) {
		reset(std::move(targets));
		return;
	}
	auto change = GlyphChange{
		.mode = GlyphMode::Scale,
		.ease = Ease::OutCubic,
		.slide = true,
		.durations = std::vector<crl::time>(targets.size(), kEditDuration),
	};
	apply(std::move(targets), *match, change, now);
}

void GlyphFlow::roll(
		std::vector<GlyphTarget> targets,
		GlyphMode mode,
		bool growing,
		crl::time now) {
	const auto count = int(targets.size());
	auto change = GlyphChange{
		.mode = mode,
		.side = growing ? -1 : 1,
		.ease = (mode == GlyphMode::Roll) ? Ease::OutCirc : Ease::OutCubic,
	};
	change.durations.reserve(count);
	for (auto i = 0; i != count; ++i) {
		change.durations.push_back((mode == GlyphMode::Roll)
			? (kSwitchFirstDuration
				+ (kSwitchDuration - kSwitchFirstDuration)
					* i
					/ std::max(count - 1, 1))
			: kEditDuration);
	}
	const auto match = PlaceMatch(_targets, targets);
	apply(std::move(targets), match, change, now);
}

void GlyphFlow::apply(
		std::vector<GlyphTarget> targets,
		const std::vector<int> &match,
		const GlyphChange &change,
		crl::time now) {
	auto alive = std::vector<int>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		if (!_slots[i].dying) {
			alive.push_back(i);
		}
	}
	if (alive.size() != _targets.size() || targets.empty()) {
		reset(std::move(targets));
		return;
	}
	const auto lefts = this->lefts(now);
	auto xs = std::vector<float64>();
	xs.reserve(_separators.size());
	for (const auto &separator : _separators) {
		xs.push_back(separatorX(separator, lefts, now));
	}
	auto result = std::vector<Slot>();
	result.reserve(_slots.size() + targets.size());
	auto killed = std::vector<uint32>();
	auto born = std::vector<uint32>();
	const auto kill = [&](Slot &&slot, crl::time duration) {
		if (!slot.dying) {
			slot.dying = true;
			slot.presence.retarget(0., now, duration, change.ease);
			if (slot.kind == GlyphKind::Group) {
				killed.push_back(slot.id);
			}
		}
		result.push_back(std::move(slot));
	};
	auto next = 0;
	for (auto j = 0; j != int(targets.size()); ++j) {
		const auto &target = targets[j];
		const auto duration = change.durations[j];
		if (match[j] < 0) {
			result.push_back(createSlot(target, change, duration, now));
			if (target.kind == GlyphKind::Group) {
				born.push_back(result.back().id);
			}
			continue;
		}
		const auto index = alive[match[j]];
		while (next < index) {
			kill(std::move(_slots[next++]), duration);
		}
		auto &slot = result.emplace_back(std::move(_slots[next++]));
		slot.kind = target.kind;
		slot.small = target.small;
		slot.place = target.place;
		slot.width.retarget(target.width, now, duration, change.ease);
		changeGlyph(slot, target, change, duration, now);
	}
	while (next < int(_slots.size())) {
		kill(std::move(_slots[next++]), change.durations.back());
	}
	_slots = std::move(result);
	_targets = std::move(targets);
	applySeparators(
		std::move(xs),
		std::move(killed),
		std::move(born),
		change,
		now);
}

void GlyphFlow::applySeparators(
		std::vector<float64> xs,
		std::vector<uint32> killed,
		std::vector<uint32> born,
		const GlyphChange &change,
		crl::time now) {
	auto orphans = std::vector<int>();
	for (auto i = 0; i != int(_separators.size()); ++i) {
		const auto &separator = _separators[i];
		if (separator.presence.to > 0.
			&& ranges::find(killed, separator.gap) != end(killed)) {
			orphans.push_back(i);
		}
	}
	ranges::sort(orphans, ranges::less(), [&](int i) {
		return slotIndex(_separators[i].gap);
	});
	if (change.slide) {
		const auto orphansCount = int(orphans.size());
		const auto bornCount = int(born.size());
		const auto count = std::min(orphansCount, bornCount);
		for (auto i = 0; i != count; ++i) {
			const auto index = orphans[orphansCount - 1 - i];
			auto &separator = _separators[index];
			const auto &gap = _slots[slotIndex(born[bornCount - 1 - i])];
			separator.ch = gap.current.ch;
			separator.gap = gap.id;
			separator.width = gap.width.to;
			separator.fromX = xs[index];
			separator.slide.jump(0.);
			separator.slide.retarget(
				1.,
				now,
				gap.presence.duration,
				change.ease);
		}
		orphans.resize(orphansCount - count);
		born.resize(bornCount - count);
	}
	for (const auto index : orphans) {
		auto &separator = _separators[index];
		separator.presence.retarget(
			0.,
			now,
			_slots[slotIndex(separator.gap)].presence.duration,
			change.ease);
	}
	const auto existing = int(_separators.size());
	for (auto i = 0; i != existing; ++i) {
		const auto index = slotIndex(_separators[i].gap);
		if (index < 0
			|| _slots[index].dying
			|| _separators[i].presence.to == 0.
			|| _separators[i].ch == _slots[index].current.ch) {
			continue;
		}
		const auto &gap = _slots[index];
		const auto duration = change.durations[ranges::count_if(
			_slots.begin(),
			_slots.begin() + index,
			[](const Slot &slot) { return !slot.dying; })];
		_separators[i].presence.retarget(0., now, duration, change.ease);
		auto added = settledSeparator(gap);
		added.presence.jump(0.);
		added.presence.retarget(1., now, duration, change.ease);
		_separators.push_back(std::move(added));
	}
	for (const auto id : born) {
		const auto &gap = _slots[slotIndex(id)];
		auto added = settledSeparator(gap);
		added.presence = gap.presence;
		_separators.push_back(std::move(added));
	}
	const auto rolls = [&](const GlyphLayer &layer) {
		return (layer.mode == GlyphMode::Roll) && layer.v.running(now);
	};
	const auto rolling = ranges::any_of(_slots, [&](const Slot &slot) {
		return rolls(slot.current) || ranges::any_of(slot.leaving, rolls);
	});
	if (rolling) {
		// Rolling digits cross the dip lane, so slides land in their gaps.
		for (auto &separator : _separators) {
			separator.slide.jump(1.);
		}
	}
}

void GlyphFlow::changeGlyph(
		Slot &slot,
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) const {
	auto &current = slot.current;
	if (slot.kind == GlyphKind::Group || current.ch == target.ch) {
		current.ch = target.ch;
		current.width = target.width;
		return;
	}
	auto entering = GlyphLayer{
		.ch = target.ch,
		.mode = change.mode,
		.side = change.side,
	};
	const auto same = [&](const GlyphLayer &layer) {
		return (layer.ch == target.ch) && (layer.mode == change.mode);
	};
	const auto revived = ranges::find_if(slot.leaving, same);
	if (revived != end(slot.leaving)) {
		entering = std::move(*revived);
		slot.leaving.erase(revived);
	}
	for (auto &layer : slot.leaving) {
		// Restart on this change's clock, so the slot never sums above 1.
		layer.v.jump(layer.v.value(now));
		layer.v.retarget(0., now, duration, change.ease);
	}
	entering.width = target.width;
	entering.v.retarget(1., now, duration, change.ease);
	if (current.v.value(now) >= 1.) {
		// All modes paint the same at v == 1, so the hand-over is seamless.
		current.mode = change.mode;
		current.side = -change.side;
	}
	current.v.retarget(0., now, duration, change.ease);
	slot.leaving.push_back(std::move(current));
	current = std::move(entering);
}

Slot GlyphFlow::createSlot(
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) {
	auto result = Slot{
		.id = ++_autoincrement,
		.kind = target.kind,
		.small = target.small,
		.place = target.place,
		.current = {
			.ch = target.ch,
			.mode = change.mode,
			.side = change.side,
			.width = target.width,
		},
	};
	result.width.jump(target.width);
	result.presence.retarget(1., now, duration, change.ease);
	if (change.mode == GlyphMode::Scale) {
		result.current.v.jump(1.);
	} else {
		result.current.v.retarget(1., now, duration, change.ease);
	}
	return result;
}

Slot GlyphFlow::settledSlot(const GlyphTarget &target) {
	auto result = Slot{
		.id = ++_autoincrement,
		.kind = target.kind,
		.small = target.small,
		.place = target.place,
		.current = {
			.ch = target.ch,
			.width = target.width,
		},
	};
	result.width.jump(target.width);
	result.presence.jump(1.);
	result.current.v.jump(1.);
	return result;
}

Separator GlyphFlow::settledSeparator(const Slot &gap) const {
	auto result = Separator{
		.ch = gap.current.ch,
		.gap = gap.id,
		.width = gap.width.to,
	};
	result.slide.jump(1.);
	result.presence.jump(1.);
	return result;
}

void GlyphFlow::prune(crl::time now) {
	const auto finished = [&](const Motion &motion) {
		return (motion.to == 0.) && !motion.running(now);
	};
	for (auto &slot : _slots) {
		slot.leaving.erase(
			ranges::remove_if(slot.leaving, finished, &GlyphLayer::v),
			end(slot.leaving));
	}
	_slots.erase(ranges::remove_if(_slots, [&](const Slot &slot) {
		return slot.dying && finished(slot.presence);
	}), end(_slots));
	_separators.erase(ranges::remove_if(_separators, [&](const auto &s) {
		return finished(s.presence) || (slotIndex(s.gap) < 0);
	}), end(_separators));
}

void GlyphFlow::finish() {
	for (auto &slot : _slots) {
		slot.width.jump(slot.width.to);
		slot.presence.jump(slot.presence.to);
		slot.current.v.jump(slot.current.v.to);
		slot.leaving.clear();
	}
	_slots.erase(ranges::remove_if(_slots, [](const Slot &slot) {
		return slot.dying;
	}), end(_slots));
	for (auto &separator : _separators) {
		separator.slide.jump(1.);
		separator.presence.jump(separator.presence.to);
	}
	_separators.erase(ranges::remove_if(_separators, [&](const auto &s) {
		return (s.presence.to == 0.) || (slotIndex(s.gap) < 0);
	}), end(_separators));
}

bool GlyphFlow::animating(crl::time now) const {
	for (const auto &slot : _slots) {
		if (slot.width.running(now)
			|| slot.presence.running(now)
			|| slot.current.v.running(now)
			|| ranges::any_of(slot.leaving, [&](const GlyphLayer &layer) {
				return layer.v.running(now);
			})) {
			return true;
		}
	}
	return ranges::any_of(_separators, [&](const Separator &separator) {
		return separator.slide.running(now)
			|| separator.presence.running(now);
	});
}

float64 GlyphFlow::width(crl::time now) const {
	auto result = 0.;
	for (const auto &slot : _slots) {
		result += slot.width.value(now) * slot.presence.value(now);
	}
	return result;
}

std::vector<float64> GlyphFlow::lefts(crl::time now) const {
	auto result = std::vector<float64>();
	result.reserve(_slots.size());
	auto left = 0.;
	for (const auto &slot : _slots) {
		result.push_back(left);
		left += slot.width.value(now) * slot.presence.value(now);
	}
	return result;
}

std::vector<float64> GlyphFlow::restLefts() const {
	auto result = std::vector<float64>();
	result.reserve(_slots.size());
	auto left = 0.;
	for (const auto &slot : _slots) {
		result.push_back(left);
		left += slot.width.to * slot.presence.to;
	}
	return result;
}

int GlyphFlow::slotIndex(uint32 id) const {
	const auto i = ranges::find(_slots, id, &Slot::id);
	return (i != end(_slots)) ? int(i - begin(_slots)) : -1;
}

float64 GlyphFlow::caretX(int position, crl::time now) const {
	const auto lefts = this->lefts(now);
	const auto index = gapIndex(position);
	return (index < int(lefts.size())) ? lefts[index] : width(now);
}

int GlyphFlow::gapIndex(int position) const {
	auto index = 0;
	auto result = 0;
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.dying || slot.kind == GlyphKind::Group) {
			continue;
		} else if (index++ == position) {
			return i;
		}
		result = i + 1;
	}
	return result;
}

float64 GlyphFlow::separatorX(
		const Separator &separator,
		const std::vector<float64> &lefts,
		crl::time now) const {
	const auto index = slotIndex(separator.gap);
	const auto gap = (index >= 0) ? lefts[index] : separator.fromX;
	return separator.fromX
		+ (gap - separator.fromX) * separator.slide.value(now);
}

std::vector<InkRange> GlyphFlow::visibleInks(
		const std::vector<float64> &lefts,
		crl::time now) const {
	auto result = std::vector<InkRange>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto presence = slot.presence.value(now);
		const auto centre = lefts[i] + slot.width.value(now) * presence / 2.;
		const auto add = [&](const GlyphLayer &layer) {
			const auto v = layer.v.value(now);
			const auto scale = presence
				* ((layer.mode == GlyphMode::Scale) ? v : 1.);
			if (presence * v <= 0. || scale <= 0.) {
				return;
			}
			const auto ink = shape(slot.small, layer.ch).ink;
			result.push_back({
				.left = centre + scale * (ink.left() - layer.width / 2.),
				.right = centre + scale * (ink.right() - layer.width / 2.),
			});
		};
		add(slot.current);
		for (const auto &layer : slot.leaving) {
			add(layer);
		}
	}
	return result;
}

std::vector<InkRange> GlyphFlow::restInks(
		const std::vector<float64> &lefts) const {
	auto result = std::vector<InkRange>();
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.dying || slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto ink = shape(slot.small, slot.current.ch).ink;
		result.push_back({
			.left = lefts[i] + ink.left(),
			.right = lefts[i] + ink.right(),
		});
	}
	return result;
}

float64 GlyphFlow::dip(
		const Separator &separator,
		float64 x,
		const std::vector<float64> &lefts,
		crl::time now) const {
	const auto ink = shape(false, separator.ch).ink;
	const auto index = slotIndex(separator.gap);
	if (!separator.slide.running(now) || ink.isEmpty() || index < 0) {
		return 0.;
	}
	const auto down = _digitsInk.bottom() - ink.top() + st::lineWidth;
	const auto up = ink.bottom() - _digitsInk.top() + st::lineWidth;
	const auto clearance = std::min(down, up);
	const auto range = [&](float64 left) {
		return InkRange{ left + ink.left(), left + ink.right() };
	};
	const auto rest = restLefts();
	const auto reach = Reach(range(x), visibleInks(lefts, now));
	const auto ramp = std::min(
		clearance,
		Reach(range(rest[index]), restInks(rest)));
	const auto factor = (reach <= 0.)
		? 1.
		: (reach >= ramp)
		? 0.
		: (1. - reach / ramp);
	return ((down <= up) ? 1. : -1.) * clearance * factor;
}

GlyphFlow::Shape GlyphFlow::shape(bool small, QChar ch) const {
	const auto key = std::make_pair(small, ch);
	const auto i = _shapes.find(key);
	if (i != end(_shapes)) {
		return i->second;
	}
	const auto &font = small ? *_small : *_big;
	auto result = Shape{
		.ink = font->metrics().tightBoundingRect(QString(ch)),
	};
	result.path.addText(0., 0., font->f, QString(ch));
	return _shapes.emplace(key, std::move(result)).first->second;
}

void GlyphFlow::paintLayer(
		QPainter &p,
		const Slot &slot,
		const GlyphLayer &layer,
		QPointF centre,
		float64 presence,
		const QColor &color,
		crl::time now) const {
	const auto v = layer.v.value(now);
	const auto scale = presence
		* ((layer.mode == GlyphMode::Scale) ? v : 1.);
	const auto opacity = presence * v;
	if (opacity <= 0. || scale <= 0.) {
		return;
	}
	const auto &font = slot.small ? *_small : *_big;
	const auto dy = (layer.mode == GlyphMode::Roll)
		? ((1. - v) * layer.side * font->height)
		: 0.;
	const auto glyph = shape(slot.small, layer.ch);
	const auto middle = glyph.ink.center().y();
	const auto was = p.opacity();
	p.save();
	p.setOpacity(was * opacity);
	p.translate(centre.x(), centre.y() + dy + middle);
	p.scale(scale, scale);
	p.translate(-layer.width / 2., -middle);
	p.fillPath(glyph.path, color);
	p.restore();
}

void GlyphFlow::paint(
		QPainter &p,
		QPointF origin,
		float64 baseline,
		const QColor &color,
		crl::time now,
		FlowGap gap) const {
	auto lefts = this->lefts(now);
	if (gap.width > 0.) {
		for (auto i = gapIndex(gap.position); i < int(lefts.size()); ++i) {
			lefts[i] += gap.width;
		}
	}
	for (auto i = 0; i != int(_slots.size()); ++i) {
		const auto &slot = _slots[i];
		if (slot.kind == GlyphKind::Group) {
			continue;
		}
		const auto presence = slot.presence.value(now);
		const auto centre = QPointF(
			origin.x() + lefts[i] + slot.width.value(now) * presence / 2.,
			origin.y() + baseline);
		for (const auto &layer : slot.leaving) {
			paintLayer(p, slot, layer, centre, presence, color, now);
		}
		paintLayer(p, slot, slot.current, centre, presence, color, now);
	}
	const auto was = p.opacity();
	for (const auto &separator : _separators) {
		const auto presence = separator.presence.value(now);
		if (presence <= 0.) {
			continue;
		}
		const auto x = separatorX(separator, lefts, now);
		const auto index = slotIndex(separator.gap);
		const auto scale = (separator.slide.running(now) || index < 0)
			? presence
			: std::min(presence, _slots[index].presence.value(now));
		const auto glyph = shape(false, separator.ch);
		const auto middle = glyph.ink.center().y();
		p.save();
		p.setOpacity(was * presence);
		p.translate(
			origin.x() + x + separator.width * scale / 2.,
			origin.y() + baseline + dip(separator, x, lefts, now) + middle);
		p.scale(scale, scale);
		p.translate(-separator.width / 2., -middle);
		p.fillPath(glyph.path, color);
		p.restore();
	}
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
, _flow(
	st::walletSendUserAmountField.style.font,
	st::walletSendUserTickerLabel.style.font)
, _blink([=] {
	_caretShown = !_caretShown;
	update(caretRect());
}) {
	_field->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	setCursor(style::cur_pointer);

	_animation.init([=](crl::time now) {
		return animationCallback(now);
	});

	rpl::combine(
		std::move(args.entryFiat),
		std::move(args.currency)
	) | rpl::on_next([=](bool fiat, QString code) {
		const auto name = Ui::CurrencyName(code);
		const auto switched = _initialized
			&& ((fiat != _fiat) || (fiat && code != _currency));
		_fiat = fiat;
		_ticker = fiat ? code : GramTicker();
		_symbol = (fiat && name != code) ? name : QString();
		_currency = std::move(code);
		if (!switched) {
			refreshContent();
			return;
		}
		_switchPending = true;
		Ui::PostponeCall(this, [=] {
			if (_switchPending) {
				refreshContent();
			}
		});
	}, lifetime());

	_field->changes() | rpl::on_next([=] {
		refreshContent();
	}, lifetime());
	const auto moved = [=] {
		if (_field->hasSelectedText()) {
			finishAnimation();
		}
		restartBlink();
		update();
		if (_field->hasFocus()) {
			QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
		}
	};
	connect(_field, &QLineEdit::cursorPositionChanged, this, moved);
	connect(_field, &QLineEdit::selectionChanged, this, moved);
	_field->compositionChanges() | rpl::on_next(moved, lifetime());
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
	_initialized = true;
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
	const auto switching = base::take(_switchPending);
	refreshLayers(switching, crl::now());
	const auto additionWidth = int(TargetWidth(_additions));
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
	applyChange(text, separator, whole, fraction, switching);
	refreshGeometry();
}

void AmountRow::refreshLayers(bool switching, crl::time now) {
	auto addition = std::optional<Layer>();
	if (!_fiat) {
		addition = Layer{
			.kind = LayerKind::Diamond,
			.width = float64(DiamondPart(
				_diamondCanvas,
				kGramDiamondRight - kGramDiamondLeft)),
		};
	} else if (!_symbol.isEmpty()) {
		addition = Layer{
			.kind = LayerKind::Symbol,
			.text = _symbol,
			.width = float64(
				st::walletSendUserAmountLabel.style.font->width(_symbol)),
			.fiat = true,
		};
	}
	auto ticker = std::optional<Layer>();
	if (!_ticker.isEmpty()) {
		ticker = Layer{
			.kind = LayerKind::Ticker,
			.text = _ticker,
			.width = float64(
				st::walletSendUserTickerLabel.style.font->width(_ticker)),
			.fiat = _fiat,
		};
	}
	ChangeLayers(_additions, std::move(addition), switching, now);
	ChangeLayers(_tickers, std::move(ticker), switching, now);
}

void AmountRow::applyChange(
		const QString &text,
		const QString &separator,
		const QString &whole,
		const QString &fraction,
		bool switching) {
	auto targets = AmountTargets(
		whole,
		fraction,
		st::walletSendUserAmountField.style.font,
		st::walletSendUserTickerLabel.style.font);
	const auto changed = (text != _shownText);
	if (!_initialized || anim::Disabled()) {
		_flow.reset(std::move(targets));
		finishAnimation();
	} else if (switching) {
		const auto growing = AmountValue(text, separator)
			> AmountValue(_shownText, _shownSeparator);
		_flow.roll(std::move(targets), GlyphMode::Roll, growing, crl::now());
		startAnimation();
	} else {
		const auto kind = ClassifyEdit(
			_shownText,
			text,
			_field->cursorPosition(),
			separator);
		_flow.edit(std::move(targets), kind, crl::now());
		startAnimation();
		if (changed && !_fiat) {
			playDiamond();
		}
	}
	_shownText = text;
	_shownSeparator = separator;
}

void AmountRow::startAnimation() {
	if (contentAnimating(crl::now()) && !_animation.animating()) {
		_animation.start();
	}
}

void AmountRow::finishContent() {
	_flow.finish();
	for (auto *layers : { &_additions, &_tickers }) {
		for (auto &layer : *layers) {
			layer.v.jump(layer.v.to);
		}
		layers->erase(ranges::remove_if(*layers, [](const Layer &layer) {
			return (layer.v.to == 0.);
		}), end(*layers));
	}
}

void AmountRow::finishAnimation() {
	finishContent();
	_animation.stop();
	update();
}

bool AmountRow::animationCallback(crl::time now) {
	if (anim::Disabled()) {
		finishContent();
		update();
		return false;
	}
	_flow.prune(now);
	for (auto *layers : { &_additions, &_tickers }) {
		layers->erase(ranges::remove_if(*layers, [&](const Layer &layer) {
			return (layer.v.to == 0.) && !layer.v.running(now);
		}), end(*layers));
	}
	update();
	return contentAnimating(now);
}

bool AmountRow::contentAnimating(crl::time now) const {
	const auto running = [&](const Layer &layer) {
		return layer.v.running(now);
	};
	return _flow.animating(now)
		|| ranges::any_of(_additions, running)
		|| ranges::any_of(_tickers, running);
}

bool AmountRow::flowPainted() const {
	return _animation.animating() || !_field->composition().isEmpty();
}

void AmountRow::playDiamond() {
	if (!_diamond
		|| _diamond->animating()
		|| !_diamond->valid()
		|| anim::Disabled()
		|| PowerSaving::On(PowerSaving::kStickersChat)) {
		return;
	}
	_diamond->animate(
		[=] { update(additionRect()); },
		0,
		_diamond->framesCount() - 1);
}

QRect AmountRow::additionRect() const {
	if (flowPainted()) {
		return rect();
	}
	const auto k = _painter.scale();
	const auto x = -DiamondPart(_diamondCanvas, kGramDiamondLeft);
	const auto y = _painter.baseline()
		- DiamondPart(_diamondCanvas, kGramDiamondBottom);
	return QRectF(
		_left + k * x,
		_top + k * y,
		k * _diamondCanvas,
		k * _diamondCanvas).toAlignedRect();
}

std::optional<QRectF> AmountRow::diamondCanvas(crl::time now) const {
	if (_fiat || !_diamond || !_diamond->valid()) {
		return std::nullopt;
	}
	const auto diamond = ranges::find(
		_additions,
		LayerKind::Diamond,
		&Layer::kind);
	if (diamond == end(_additions)
		|| diamond->v.to != 1.
		|| diamond->v.value(now) < 1.) {
		return std::nullopt;
	}
	const auto animated = flowPainted();
	const auto geometry = animated ? animatedGeometry(now) : RowGeometry();
	const auto k = animated ? geometry.k : _painter.scale();
	const auto left = animated ? geometry.left : _left;
	const auto top = animated ? geometry.top : _top;
	const auto allotted = animated
		? geometry.additions
		: float64(_painter.wholeLeft());
	const auto full = diamond->width + st::walletDetailsAmountMinorSkip;
	const auto scale = (allotted < full) ? (allotted / full) : 1.;
	if (scale <= 0.) {
		return std::nullopt;
	}
	const auto baseline = float64(_painter.baseline());
	const auto middle = baseline - _figureBig / 2.;
	const auto x = -DiamondPart(_diamondCanvas, kGramDiamondLeft);
	const auto y = baseline - DiamondPart(_diamondCanvas, kGramDiamondBottom);
	return QRectF(
		left + k * scale * x,
		top + k * (middle + scale * (y - middle)),
		k * scale * _diamondCanvas,
		k * scale * _diamondCanvas);
}

AmountDiamond AmountRow::takeDiamond() {
	const auto canvas = diamondCanvas(crl::now());
	if (!canvas) {
		return {};
	}
	_diamond->jumpTo(_diamond->frameIndex(), nullptr);
	auto result = AmountDiamond{
		.icon = std::move(_diamond),
		.global = canvas->translated(QPointF(mapToGlobal(QPoint()))),
	};
	update();
	return result;
}

RowGeometry AmountRow::animatedGeometry(crl::time now) const {
	const auto gap = float64(st::walletDetailsAmountMinorSkip);
	auto result = RowGeometry{
		.flow = _flow.width(now),
		.composition = compositionFont()->metrics().horizontalAdvance(
			_field->composition()),
	};
	for (const auto &layer : _additions) {
		if (layer.width > 0.) {
			result.additions += layer.v.value(now) * (layer.width + gap);
		}
	}
	auto tail = 0.;
	for (const auto &layer : _tickers) {
		const auto v = layer.v.value(now);
		result.tickerSkip += v * gap;
		tail += v * (gap + layer.width);
	}
	const auto natural = result.additions
		+ result.flow
		+ result.composition
		+ tail;
	const auto available = width();
	result.k = (available > 0 && natural > available)
		? (available / natural)
		: 1.;
	result.left = std::max(
		std::floor((available - result.k * natural) / 2.),
		0.);
	result.top = std::floor(
		(height() - result.k * _painter.naturalHeight()) / 2.);
	return result;
}

void AmountRow::refreshGeometry() {
	_painter.setAvailableWidth(width());
	const auto k = _painter.scale();
	_left = std::max(
		std::floor((width() - k * _painter.naturalWidth()) / 2.),
		0.);
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

int AmountRow::caretPosition() const {
	return std::clamp(
		_field->cursorPosition(),
		0,
		std::max(int(_layout.caret.size()) - 1, 0));
}

bool AmountRow::caretInFraction() const {
	return (_layout.separatorAt >= 0)
		&& (caretPosition() > _layout.separatorAt);
}

const style::font &AmountRow::compositionFont() const {
	return caretInFraction()
		? st::walletSendUserTickerLabel.style.font
		: st::walletSendUserAmountField.style.font;
}

QRect AmountRow::caretRect() const {
	if (_layout.caret.empty()) {
		return QRect();
	}
	if (!_field->composition().isEmpty()) {
		const auto now = crl::now();
		return flowCaretRect(animatedGeometry(now), now);
	}
	return caretRect(
		_painter.scale(),
		_left,
		_top,
		_layout.caret[caretPosition()]);
}

QRect AmountRow::caretRect(
		float64 k,
		float64 left,
		float64 top,
		float64 x) const {
	const auto figure = caretInFraction() ? _figureSmall : _figureBig;
	const auto baseline = _painter.baseline();
	const auto from = base::SafeRound(left + k * x);
	const auto upper = base::SafeRound(top + k * (baseline - figure));
	const auto lower = base::SafeRound(top + k * baseline);
	const auto width = std::max(
		_field->style()->pixelMetric(
			QStyle::PM_TextCursorWidth,
			nullptr,
			_field),
		1);
	return QRect(int(from), int(upper), width, int(lower - upper));
}

QRect AmountRow::flowCaretRect(
		const RowGeometry &geometry,
		crl::time now) const {
	return caretRect(
		geometry.k,
		geometry.left,
		geometry.top,
		(geometry.additions
			+ _flow.caretX(caretPosition(), now)
			+ geometry.composition));
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

void AmountRow::paintAddition(
		QPainter &p,
		float64 allotted,
		crl::time now) const {
	const auto baseline = _painter.baseline();
	const auto middle = baseline - _figureBig / 2.;
	const auto gap = float64(st::walletDetailsAmountMinorSkip);
	const auto opacity = p.opacity();
	for (const auto &layer : _additions) {
		const auto full = layer.width + gap;
		const auto scale = (allotted < full) ? (allotted / full) : 1.;
		const auto v = layer.v.value(now);
		if (v <= 0. || scale <= 0.) {
			continue;
		}
		p.save();
		p.setOpacity(opacity * v);
		p.translate(0., middle);
		p.scale(scale, scale);
		p.translate(0., -middle);
		if (layer.kind == LayerKind::Diamond) {
			if (_diamond) {
				_diamond->paint(
					p,
					-DiamondPart(_diamondCanvas, kGramDiamondLeft),
					baseline - DiamondPart(_diamondCanvas, kGramDiamondBottom));
			}
		} else {
			p.setFont(st::walletSendUserAmountLabel.style.font);
			p.setPen(LayerColor(layer));
			p.drawText(QPointF(0., baseline), layer.text);
		}
		p.restore();
	}
}

void AmountRow::paintTickers(
		QPainter &p,
		float64 left,
		crl::time now) const {
	auto allotted = 0.;
	for (const auto &layer : _tickers) {
		allotted += layer.v.value(now) * layer.width;
	}
	const auto baseline = float64(_painter.baseline());
	const auto middle = baseline - _figureSmall / 2.;
	const auto &font = st::walletSendUserTickerLabel.style.font;
	const auto opacity = p.opacity();
	for (const auto &layer : _tickers) {
		const auto scale = (allotted < layer.width)
			? (allotted / layer.width)
			: 1.;
		const auto v = layer.v.value(now);
		if (v <= 0. || scale <= 0.) {
			continue;
		}
		auto path = QPainterPath();
		path.addText(0., baseline - middle, font->f, layer.text);
		p.save();
		p.setOpacity(opacity * v);
		p.translate(left, middle);
		p.scale(scale, scale);
		p.fillPath(path, LayerColor(layer)->c);
		p.restore();
	}
}

void AmountRow::paintComposition(
		QPainter &p,
		float64 left,
		float64 baseline) const {
	auto path = QPainterPath();
	path.addText(
		left,
		baseline,
		compositionFont()->underline()->f,
		_field->composition());
	p.fillPath(path, st::walletSendUserAmountField.textFg->c);
}

void AmountRow::paintAnimated(QPainter &p, crl::time now) const {
	const auto geometry = animatedGeometry(now);
	const auto baseline = float64(_painter.baseline());
	const auto &fg = st::walletSendUserAmountField.textFg;
	p.translate(geometry.left, geometry.top);
	p.scale(geometry.k, geometry.k);
	paintAddition(p, geometry.additions, now);
	const auto position = caretPosition();
	_flow.paint(
		p,
		QPointF(geometry.additions, 0.),
		baseline,
		fg->c,
		now,
		{ .position = position, .width = geometry.composition });
	if (geometry.composition > 0.) {
		paintComposition(
			p,
			geometry.additions + _flow.caretX(position, now),
			baseline);
	}
	paintTickers(
		p,
		(geometry.additions
			+ geometry.flow
			+ geometry.composition
			+ geometry.tickerSkip),
		now);
	p.resetTransform();
	if (_caretShown && _field->hasFocus()) {
		p.fillRect(flowCaretRect(geometry, now), fg);
	}
}

void AmountRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto now = crl::now();
	if (flowPainted()) {
		paintAnimated(p, now);
		return;
	}
	const auto k = _painter.scale();
	const auto &fg = st::walletSendUserAmountField.textFg;
	const auto ticker = (_fiat
		? st::walletSendUserFiatFg
		: st::walletSendUserGramFg)->c;
	p.translate(_left, _top);
	p.scale(k, k);
	paintAddition(p, _painter.wholeLeft(), now);
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
	_field->commitComposition();
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
	_field->commitComposition();
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

EquivalentLabel::EquivalentLabel(
	QWidget *parent,
	Ui::Text::MarkedContext context)
: RpWidget(parent)
, _context(std::move(context))
, _flow(
	st::walletSendUserFiatButton.style.font,
	st::walletSendUserFiatButton.style.font) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_context.repaint = [=] { update(); };
	_arrows = createPart(tr::marked(u" ↑↓"_q));
	_animation.init([=](crl::time now) {
		return animationCallback(now);
	});
}

rpl::producer<int> EquivalentLabel::labelWidthValue() const {
	return _width.value();
}

void EquivalentLabel::setLabel(AmountLabel label) {
	if (_initialized && label == _label) {
		return;
	}
	const auto &st = st::walletSendUserFiatButton;
	auto full = label.prefix;
	full.append(label.amount).append(label.suffix).append(_arrows.source);
	_static.setMarkedText(st.style, full, kMarkupTextOptions, _context);
	auto targets = LabelTargets(label);
	const auto now = crl::now();
	if (!_initialized || anim::Disabled()) {
		_flow.reset(targets);
		_prefixes.clear();
		_prefixes.push_back(createPart(label.prefix));
		_suffixes.clear();
		_suffixes.push_back(createPart(tr::marked(label.suffix)));
		_targets = std::move(targets);
		_extra.jump(_static.maxWidth() - restWidth());
		_animation.stop();
	} else {
		const auto switched = (label.unit != _label.unit);
		const auto growing = AmountValue(label.amount, label.decimal)
			> AmountValue(_label.amount, _label.decimal);
		const auto duration = switched ? kSwitchFadeDuration : kEditDuration;
		_flow.roll(
			targets,
			switched ? GlyphMode::Roll : GlyphMode::Fade,
			growing,
			now);
		crossFade(_prefixes, label.prefix, duration, now);
		crossFade(_suffixes, tr::marked(label.suffix), duration, now);
		_targets = std::move(targets);
		_extra.retarget(
			_static.maxWidth() - restWidth(),
			now,
			switched ? kSwitchDuration : kEditDuration);
		if (animating(now) && !_animation.animating()) {
			_animation.start();
		}
	}
	_label = std::move(label);
	_initialized = true;
	refreshWidth(now);
	update();
}

QString EquivalentLabel::plainText() const {
	return _label.prefix.text
		+ _label.amount
		+ _label.suffix
		+ _arrows.source.text;
}

LabelPart EquivalentLabel::createPart(const TextWithEntities &source) const {
	const auto &st = st::walletSendUserFiatButton.style;
	const auto &text = source.text;
	const auto size = int(text.size());
	auto lead = 0;
	while (lead < size && text[lead] == QChar(' ')) {
		++lead;
	}
	auto trail = 0;
	while (trail < size - lead && text[size - 1 - trail] == QChar(' ')) {
		++trail;
	}
	auto result = LabelPart{ .source = source };
	result.text.setMarkedText(st, source, kMarkupTextOptions, _context);
	const auto space = float64(st.font->spacew);
	result.lead = lead * space;
	result.width = result.lead + result.text.maxWidth() + trail * space;
	result.v.jump(1.);
	return result;
}

void EquivalentLabel::crossFade(
		std::vector<LabelPart> &parts,
		const TextWithEntities &source,
		crl::time duration,
		crl::time now) {
	auto found = false;
	for (auto &part : parts) {
		const auto mine = !found && (part.source == source);
		found = found || mine;
		part.v.retarget(mine ? 1. : 0., now, duration);
	}
	if (!found) {
		auto &added = parts.emplace_back(createPart(source));
		added.v.jump(0.);
		added.v.retarget(1., now, duration);
	}
}

void EquivalentLabel::finishContent() {
	_flow.finish();
	for (auto *parts : { &_prefixes, &_suffixes }) {
		for (auto &part : *parts) {
			part.v.jump(part.v.to);
		}
		parts->erase(ranges::remove_if(*parts, [](const LabelPart &part) {
			return (part.v.to == 0.);
		}), end(*parts));
	}
	_extra.jump(_extra.to);
}

bool EquivalentLabel::animationCallback(crl::time now) {
	const auto disabled = anim::Disabled();
	if (disabled) {
		finishContent();
	} else {
		_flow.prune(now);
		for (auto *parts : { &_prefixes, &_suffixes }) {
			parts->erase(ranges::remove_if(*parts, [&](const LabelPart &part) {
				return (part.v.to == 0.) && !part.v.running(now);
			}), end(*parts));
		}
	}
	const auto result = !disabled && animating(now);
	refreshWidth(result ? now : 0);
	update();
	return result;
}

bool EquivalentLabel::animating(crl::time now) const {
	const auto running = [&](const LabelPart &part) {
		return part.v.running(now);
	};
	return _flow.animating(now)
		|| _extra.running(now)
		|| ranges::any_of(_prefixes, running)
		|| ranges::any_of(_suffixes, running);
}

float64 EquivalentLabel::restWidth() const {
	auto result = _arrows.width;
	for (const auto &target : _targets) {
		result += target.width;
	}
	for (const auto *parts : { &_prefixes, &_suffixes }) {
		for (const auto &part : *parts) {
			result += part.width * part.v.to;
		}
	}
	return result;
}

float64 EquivalentLabel::animatedWidth(crl::time now) const {
	auto result = _arrows.width + _flow.width(now) + _extra.value(now);
	for (const auto *parts : { &_prefixes, &_suffixes }) {
		for (const auto &part : *parts) {
			result += part.width * part.v.value(now);
		}
	}
	return result;
}

void EquivalentLabel::refreshWidth(crl::time now) {
	_width = (now && _animation.animating())
		? int(base::SafeRound(animatedWidth(now)))
		: _static.maxWidth();
}

float64 EquivalentLabel::paintParts(
		QPainter &p,
		std::span<const LabelPart> parts,
		float64 left,
		crl::time now) const {
	auto allotted = 0.;
	for (const auto &part : parts) {
		allotted += part.width * part.v.value(now);
	}
	const auto &st = st::walletSendUserFiatButton;
	const auto middle = st.style.font->height / 2.;
	auto palette = st::defaultTextPalette;
	palette.linkFg = st.numbersTextFg;
	const auto opacity = p.opacity();
	for (const auto &part : parts) {
		const auto v = part.v.value(now);
		const auto scale = (allotted < part.width)
			? (allotted / part.width)
			: 1.;
		if (v <= 0. || scale <= 0. || part.text.isEmpty()) {
			continue;
		}
		p.save();
		p.setOpacity(opacity * v);
		p.translate(left, st.padding.top() + st.textTop + middle);
		p.scale(scale, scale);
		p.translate(part.lead, -middle);
		part.text.draw(p, {
			.position = { 0, 0 },
			.availableWidth = part.text.maxWidth(),
			.palette = &palette,
		});
		p.restore();
	}
	return allotted;
}

void EquivalentLabel::paintAnimated(QPainter &p, crl::time now) const {
	const auto &st = st::walletSendUserFiatButton;
	const auto padding = st.padding.left() + st.padding.right();
	const auto top = float64(st.padding.top() + st.textTop);
	auto x = st.padding.left() + (width() - animatedWidth(now) - padding) / 2.;
	x += paintParts(p, _prefixes, x, now);
	_flow.paint(
		p,
		QPointF(x, top),
		st.style.font->ascent,
		st.textFg->c,
		now);
	x += _flow.width(now);
	x += paintParts(p, _suffixes, x, now);
	paintParts(p, { &_arrows, 1 }, x, now);
}

void EquivalentLabel::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &st = st::walletSendUserFiatButton;
	p.setPen(st.textFg);
	if (_animation.animating()) {
		auto hq = PainterHighQualityEnabler(p);
		paintAnimated(p, crl::now());
		return;
	}
	const auto padding = st.padding.left() + st.padding.right();
	const auto inner = std::min(_static.maxWidth(), width() - padding);
	auto palette = st::defaultTextPalette;
	palette.linkFg = st.numbersTextFg;
	_static.draw(p, {
		.position = {
			st.padding.left() + (width() - inner - padding) / 2,
			st.padding.top() + st.textTop,
		},
		.availableWidth = std::max(inner, 0),
		.palette = &palette,
		.elisionLines = 1,
	});
}

void SwapPill::setAccessibleText(QString text) {
	if (_accessibleText != text) {
		_accessibleText = std::move(text);
		accessibilityNameChanged();
	}
}

QString SwapPill::accessibilityName() {
	return _accessibleText;
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
	const auto &st = st::walletSendUserFiatButton;
	const auto pill = Ui::CreateChild<SwapPill>(
		wrap,
		rpl::single(QString()),
		st);
	pill->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	pill->setClickedCallback(std::move(args.swap));
	const auto label = Ui::CreateChild<EquivalentLabel>(
		wrap,
		args.equivalentContext);
	std::move(args.equivalent) | rpl::on_next([=](AmountLabel value) {
		label->setLabel(std::move(value));
		pill->setAccessibleText(label->plainText());
	}, label->lifetime());
	std::move(args.equivalentShown) | rpl::on_next([=](bool shown) {
		pill->setVisible(shown);
		label->setVisible(shown);
	}, pill->lifetime());
	const auto band = AmountBand();
	rpl::combine(
		wrap->widthValue(),
		label->labelWidthValue()
	) | rpl::on_next([=](int width, int labelWidth) {
		const auto natural = labelWidth
			- st.width
			+ st.padding.left()
			+ st.padding.right();
		row->setGeometry(0, 0, width, band);
		pill->resize(std::min(natural, width), pill->height());
		pill->moveToLeft(
			(width - pill->width()) / 2,
			band + st::walletSendFieldMargin.top(),
			width);
		label->setGeometry(pill->geometry());
		wrap->resize(width, pill->y() + pill->height());
	}, wrap->lifetime());
	return row->field();
}

AmountDiamond TakeAmountDiamond(not_null<Ui::TonAmountInput*> field) {
	if (const auto row = dynamic_cast<AmountRow*>(field->parentWidget())) {
		return row->takeDiamond();
	}
	return {};
}

} // namespace Wallet
