/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_bot_menu_hold.h"

#include <QtGui/QtEvents>

namespace HistoryView::Controls {

BotMenuHoldTracker::BotMenuHoldTracker(
		not_null<Ui::AbstractButton*> watch,
		crl::time duration,
		Fn<void()> activated)
: _watch(watch)
, _duration(duration)
, _activated(std::move(activated)) {
	base::install_event_filter(watch, [=](not_null<QEvent*> event) {
		return filter(event);
	}, watch->lifetime());
	_timer.setCallback([=] {
		fire();
	});
}

BotMenuHoldTracker::~BotMenuHoldTracker() {
	_timer.cancel();
}

base::EventFilterResult BotMenuHoldTracker::filter(not_null<QEvent*> event) {
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const auto e = static_cast<QMouseEvent*>(event.get());
		if (e->button() == Qt::LeftButton) {
			_timer.callOnce(_duration);
		} else if (e->button() == Qt::RightButton) {
			fire();
		}
		break;
	}
	case QEvent::MouseButtonRelease:
		_timer.cancel();
		break;
	default:
		break;
	}
	return base::EventFilterResult::Continue;
}

void BotMenuHoldTracker::fire() {
	const auto watch = _watch;
	const auto activated = _activated;
	activated();
	if (watch) {
		watch->clearState();
	}
}

} // namespace HistoryView::Controls
