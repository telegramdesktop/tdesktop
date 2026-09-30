/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_amount_painter.h"

namespace Wallet {

AmountPainter::AmountPainter(const AmountStyle &st, AmountParts parts) {
	setContent(st, std::move(parts));
}

void AmountPainter::setContent(const AmountStyle &st, AmountParts parts) {
	_st = st;
	_parts = std::move(parts);
	const auto &big = _st.big;
	const auto &small = _st.small;
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
	_naturalWidth = _tickerLeft + (ticker.isEmpty() ? 0 : big->width(ticker));

	_digits = QPainterPath();
	_ticker = QPainterPath();
	if (!whole.isEmpty()) {
		_digits.addText(_wholeLeft, big->ascent, big, whole);
	}
	if (!fraction.isEmpty()) {
		_digits.addText(_fractionLeft, big->ascent, small, fraction);
	}
	if (!ticker.isEmpty()) {
		_ticker.addText(_tickerLeft, big->ascent, big, ticker);
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
	p.fillPath(_digits, colors.digits);
	if (_ticker.isEmpty()) {
		return;
	}
	const auto opacity = p.opacity();
	p.setOpacity(opacity * colors.tickerOpacity);
	p.fillPath(_ticker, colors.ticker);
	p.setOpacity(opacity);
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

} // namespace Wallet
