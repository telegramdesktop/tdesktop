/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_empty_list_bubble.h"

#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_service_message.h"

namespace HistoryView {

EmptyListBubbleWidget::EmptyListBubbleWidget(
	not_null<ListWidget*> parent,
	const style::margins &padding)
: RpWidget(parent)
, _padding(padding) {

	parent->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		updateGeometry(s);
	}, lifetime());
}

void EmptyListBubbleWidget::updateGeometry(const QSize &size) {
	const auto w = _forceWidth
		? _forceWidth
		: std::min(
			_text.maxWidth() + _padding.left() + _padding.right(),
			size.width());
	_innerWidth = w - _padding.left() - _padding.right();
	const auto h = _padding.top()
		+ _text.countHeight(_innerWidth)
		+ _padding.bottom();
	resize(w, h);
	move((size.width() - w) / 2, (size.height() - h) / 3);
}

void EmptyListBubbleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto r = rect();
	HistoryView::ServiceMessagePainter::paintBubble(
		p,
		r.x(),
		r.y(),
		r.width(),
		r.height());

	p.setPen(st::msgServiceFg);
	_text.draw(
		p,
		r.x() + _padding.left(),
		r.y() + _padding.top(),
		_innerWidth,
		style::al_top);
}

void EmptyListBubbleWidget::setText(
		const TextWithEntities &textWithEntities) {
	auto options = _defaultOptions;
	options.flags |= TextParseMarkdown;
	_text.setMarkedText(st::defaultTextStyle, textWithEntities, options);
	updateGeometry(size());
}

void EmptyListBubbleWidget::setForceWidth(int width) {
	if (_forceWidth != width) {
		_forceWidth = width;
		updateGeometry(size());
	}
}

} // namespace HistoryView
