/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/continuous_scroll.h"

#include <QScrollBar>
#include <QWheelEvent>

namespace Ui {

void ContinuousScroll::wheelEvent(QWheelEvent *e) {
	if (_tracking
		&& !e->angleDelta().isNull()
		&& (e->angleDelta().y() < 0)
		&& (scrollTopMax() == scrollTop())) {
		_addContentRequests.fire({});
		if (base::take(_contentAdded)) {
			viewportEvent(e);
		}
		return;
	}
	ScrollArea::wheelEvent(e);
}

void ContinuousScroll::setTrackingContent(bool value) {
	if (_tracking == value) {
		return;
	}
	_tracking = value;
	reconnect();
}

void ContinuousScroll::reconnect() {
	if (!_tracking) {
		_connection.release();
		return;
	}
	const auto handleAction = [=](int action) {
		const auto scroll = verticalScrollBar();
		const auto step = (action == QAbstractSlider::SliderSingleStepAdd)
			? scroll->singleStep()
			: (action == QAbstractSlider::SliderPageStepAdd)
			? scroll->pageStep()
			: 0;
		if (!action) {
			return;
		}
		const auto newTop = scrollTop() + step;
		if (newTop > scrollTopMax()) {
			_addContentRequests.fire({});
			if (base::take(_contentAdded)) {
				scroll->setSliderPosition(newTop);
			}
		}
	};
	_connection = QObject::connect(
		verticalScrollBar(),
		&QAbstractSlider::actionTriggered,
		handleAction);
}

void ContinuousScroll::contentAdded() {
	_contentAdded = true;
}

rpl::producer<> ContinuousScroll::addContentRequests() const {
	return _addContentRequests.events();
}

} // namespace Ui
