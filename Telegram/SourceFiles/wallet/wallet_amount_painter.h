/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Wallet {

struct AmountParts {
	QString whole;
	QString fraction;
	QString ticker;
};

struct AmountStyle {
	style::font big;
	style::font small;
	int additionWidth = 0;
	int additionSkip = 0;
	int tickerSkip = 0;
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

private:
	void refreshScale();

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
