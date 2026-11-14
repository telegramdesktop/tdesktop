/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_touch_scroll.h"

#include "ui/widgets/elastic_scroll.h"

namespace HistoryView {

TouchScroll::TouchScroll(
	Ui::ElasticScroll *scroll,
	Fn<bool(QPoint delta, bool pressed)> fallback)
: _scroll(scroll)
, _fallback(std::move(fallback)) {
}

void TouchScroll::start(QPoint travel) {
	_elastic = _scroll && (std::abs(travel.x()) <= std::abs(travel.y()));
}

bool TouchScroll::fling(QPoint delta) {
	return scrollBy(delta, false);
}

void TouchScroll::release() {
	if (_scroll) {
		_scroll->touchScrollRelease();
	}
}

void TouchScroll::finish(bool cancelled) {
	if (_scroll) {
		_scroll->touchScrollFinish(cancelled);
	}
}

bool TouchScroll::drag(QPoint delta) {
	const auto weak = base::make_weak(this);
	scrollBy(delta, true);
	return (weak != nullptr);
}

bool TouchScroll::scrollBy(QPoint delta, bool pressed) {
	return _elastic
		? _scroll->touchScrollBy(QPoint(0, delta.y()), pressed)
		: _fallback(delta, pressed);
}

} // namespace HistoryView
