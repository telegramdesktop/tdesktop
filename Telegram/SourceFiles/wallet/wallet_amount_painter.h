/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Wallet {

// WHY: the Gram lotties draw the diamond in a larger canvas, with a faint glow
// above the top edge, so the amount row sizes and places the canvas by the
// drawn edges of the resting frame, the glow excluded.
inline constexpr auto kGramDiamondLeft = 35. / 512.;
inline constexpr auto kGramDiamondTop = 107. / 512.;
inline constexpr auto kGramDiamondRight = 477. / 512.;
inline constexpr auto kGramDiamondBottom = 489. / 512.;

[[nodiscard]] int GramDiamondCanvas(const style::font &font);

// A currency code like the fiat codes shown in its place, so not translated.
[[nodiscard]] QString GramTicker();

struct AmountParts {
	QString whole;
	QString fraction;
	QString ticker;
};

struct AmountStyle {
	style::font big;
	style::font small;
	style::font ticker;
	int additionWidth = 0;
	int additionSkip = 0;
	int tickerSkip = 0;
	// Hinting snaps figure heights, so only amounts once drawn as text opt in.
	bool hinted = false;
};

struct AmountColors {
	QColor digits;
	QColor ticker;
	float64 tickerOpacity = 1.;
};

// Shrinks the whole group uniformly when it is wider than the available width.
class AmountPainter final {
public:
	AmountPainter() = default;
	AmountPainter(const AmountStyle &st, AmountParts parts);

	void setContent(const AmountStyle &st, AmountParts parts);
	void setAvailableWidth(int available);

	[[nodiscard]] const AmountParts &parts() const;
	[[nodiscard]] int naturalWidth() const;
	[[nodiscard]] int naturalHeight() const;
	[[nodiscard]] int baseline() const;
	[[nodiscard]] int wholeLeft() const;
	[[nodiscard]] int fractionLeft() const;
	[[nodiscard]] int tickerLeft() const;
	[[nodiscard]] float64 scale() const;
	[[nodiscard]] QSizeF size() const;

	void paint(QPainter &p, const AmountColors &colors) const;
	void paint(
		QPainter &p,
		QPointF topLeft,
		const AmountColors &colors) const;
	// |digits| are how far each digit, left to right, has rolled up from 0.
	void paintRolling(
		QPainter &p,
		QPointF topLeft,
		const AmountColors &colors,
		const std::vector<float64> &digits) const;

private:
	void refreshScale();
	void paintText(QPainter &p, const AmountColors &colors) const;

	AmountStyle _st;
	AmountParts _parts;
	QPainterPath _digits;
	QPainterPath _ticker;
	float64 _scale = 1.;
	int _wholeLeft = 0;
	int _fractionLeft = 0;
	int _tickerLeft = 0;
	int _naturalWidth = 0;
	int _available = 0;

};

} // namespace Wallet
