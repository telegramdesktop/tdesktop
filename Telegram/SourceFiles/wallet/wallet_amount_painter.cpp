/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_amount_painter.h"

#include "base/platform/base_platform_info.h"

#include <QtGui/QTextLayout>

namespace Wallet {
namespace {

[[nodiscard]] std::vector<float64> GlyphLefts(
		const style::font &font,
		const QString &text) {
	auto result = std::vector<float64>();
	if (text.isEmpty()) {
		return result;
	}
	auto layout = QTextLayout(text, font->f);
	auto option = layout.textOption();
	option.setUseDesignMetrics(true);
	layout.setTextOption(option);
	layout.beginLayout();
	const auto line = layout.createLine();
	layout.endLayout();
	result.reserve(text.size());
	for (auto i = 0; i != int(text.size()); ++i) {
		result.push_back(line.cursorToX(i));
	}
	return result;
}

} // namespace

QString GramTicker() {
	return u"GRAM"_q;
}

int GramDiamondCanvas(const style::font &font) {
	const auto figure = int(base::SafeRound(
		-font->metrics().tightBoundingRect(u"0123456789"_q).top()));
	return int(base::SafeRound(
		figure / (kGramDiamondBottom - kGramDiamondTop)));
}

AmountPainter::AmountPainter(const AmountStyle &st, AmountParts parts) {
	setContent(st, std::move(parts));
}

void AmountPainter::setContent(const AmountStyle &st, AmountParts parts) {
	_st = st;
	_parts = std::move(parts);
	const auto &big = _st.big;
	const auto &small = _st.small;
	const auto &tickerFont = _st.ticker ? _st.ticker : _st.big;
	const auto &whole = _parts.whole;
	const auto &fraction = _parts.fraction;
	const auto &ticker = _parts.ticker;
	_wholeLeft = (_st.additionWidth > 0)
		? (_st.additionWidth + _st.additionSkip)
		: 0;
	_fractionLeft = _wholeLeft + big->width(whole);
	const auto fractionWidth = fraction.isEmpty() ? 0 : small->width(fraction);
	_tickerLeft = _fractionLeft
		+ fractionWidth
		+ (ticker.isEmpty() ? 0 : _st.tickerSkip);
	_naturalWidth = _tickerLeft
		+ (ticker.isEmpty() ? 0 : tickerFont->width(ticker));

	_digits = QPainterPath();
	_ticker = QPainterPath();
	if (!whole.isEmpty()) {
		_digits.addText(_wholeLeft, big->ascent, big, whole);
	}
	if (!fraction.isEmpty()) {
		_digits.addText(_fractionLeft, big->ascent, small, fraction);
	}
	if (!ticker.isEmpty()) {
		_ticker.addText(_tickerLeft, big->ascent, tickerFont, ticker);
	}
	refreshScale();
}

void AmountPainter::setAvailableWidth(int available) {
	_available = available;
	refreshScale();
}

void AmountPainter::refreshScale() {
	_scale = (_available > 0 && _naturalWidth > _available)
		? (_available / float64(_naturalWidth))
		: 1.;
}

const AmountParts &AmountPainter::parts() const {
	return _parts;
}

int AmountPainter::naturalWidth() const {
	return _naturalWidth;
}

int AmountPainter::naturalHeight() const {
	return _st.big ? _st.big->height : 0;
}

int AmountPainter::baseline() const {
	return _st.big ? _st.big->ascent : 0;
}

int AmountPainter::wholeLeft() const {
	return _wholeLeft;
}

int AmountPainter::fractionLeft() const {
	return _fractionLeft;
}

int AmountPainter::tickerLeft() const {
	return _tickerLeft;
}

float64 AmountPainter::scale() const {
	return _scale;
}

QSizeF AmountPainter::size() const {
	return QSizeF(_naturalWidth, naturalHeight()) * _scale;
}

void AmountPainter::paint(QPainter &p, const AmountColors &colors) const {
	// WHY: glyphs are hinted only under an unscaled device transform; CoreText
	// draws text heavier than the paths, and Windows ClearType text differs in
	// weight and shape from the grayscale paths, so switching at a bump jumps.
	if (_st.hinted
		&& Platform::IsLinux()
		&& p.deviceTransform().type() <= QTransform::TxTranslate) {
		paintText(p, colors);
		return;
	}
	p.fillPath(_digits, colors.digits);
	if (_ticker.isEmpty()) {
		return;
	}
	const auto opacity = p.opacity();
	p.setOpacity(opacity * colors.tickerOpacity);
	p.fillPath(_ticker, colors.ticker);
	p.setOpacity(opacity);
}

void AmountPainter::paintText(QPainter &p, const AmountColors &colors) const {
	const auto pen = p.pen();
	const auto font = p.font();
	const auto y = float64(baseline());
	p.setPen(colors.digits);
	if (!_parts.whole.isEmpty()) {
		p.setFont(_st.big);
		p.drawText(QPointF(_wholeLeft, y), _parts.whole);
	}
	if (!_parts.fraction.isEmpty()) {
		p.setFont(_st.small);
		p.drawText(QPointF(_fractionLeft, y), _parts.fraction);
	}
	if (!_parts.ticker.isEmpty()) {
		const auto opacity = p.opacity();
		p.setOpacity(opacity * colors.tickerOpacity);
		p.setPen(colors.ticker);
		p.setFont(_st.ticker ? _st.ticker : _st.big);
		p.drawText(QPointF(_tickerLeft, y), _parts.ticker);
		p.setOpacity(opacity);
	}
	p.setFont(font);
	p.setPen(pen);
}

void AmountPainter::paint(
		QPainter &p,
		QPointF topLeft,
		const AmountColors &colors) const {
	p.save();
	p.translate(topLeft);
	p.scale(_scale, _scale);
	paint(p, colors);
	p.restore();
}

void AmountPainter::paintRolling(
		QPainter &p,
		QPointF topLeft,
		const AmountColors &colors,
		const std::vector<float64> &digits) const {
	p.save();
	p.translate(topLeft);
	p.scale(_scale, _scale);
	const auto opacity = p.opacity();
	const auto y = float64(baseline());
	auto still = QPainterPath();
	const auto add = [&](
			const style::font &font,
			QPointF position,
			QChar ch,
			float64 alpha) {
		if (alpha >= 1.) {
			still.addText(position, font, QString(ch));
		} else if (alpha > 0.) {
			auto path = QPainterPath();
			path.addText(position, font, QString(ch));
			p.setOpacity(opacity * alpha);
			p.fillPath(path, colors.digits);
		}
	};
	auto index = 0;
	const auto part = [&](
			const style::font &font,
			const QString &text,
			int left) {
		const auto lefts = GlyphLefts(font, text);
		const auto &metrics = font->metrics();
		for (auto i = 0; i != int(text.size()); ++i) {
			const auto ch = text[i];
			const auto x = left + lefts[i];
			if (!ch.isDigit() || index >= int(digits.size())) {
				add(font, QPointF(x, y), ch, 1.);
				continue;
			}
			const auto rolled = digits[index++];
			const auto shown = int(std::floor(rolled));
			const auto v = rolled - shown;
			const auto zero = ch.unicode() - ch.digitValue();
			const auto width = metrics.horizontalAdvance(ch);
			for (const auto layer : { 0, 1 }) {
				const auto face = QChar(zero + (shown + layer) % 10);
				const auto skip = (width - metrics.horizontalAdvance(face)) / 2.;
				add(
					font,
					QPointF(x + skip, y + (layer - v) * font->height),
					face,
					layer ? v : (1. - v));
			}
		}
	};
	part(_st.big, _parts.whole, _wholeLeft);
	part(_st.small, _parts.fraction, _fractionLeft);
	p.setOpacity(opacity);
	p.fillPath(still, colors.digits);
	if (!_ticker.isEmpty()) {
		p.setOpacity(opacity * colors.tickerOpacity);
		p.fillPath(_ticker, colors.ticker);
	}
	p.restore();
}

} // namespace Wallet
