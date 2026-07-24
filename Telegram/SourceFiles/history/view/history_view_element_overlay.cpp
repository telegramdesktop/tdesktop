/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element_overlay.h"

#include "history/view/history_view_element.h"
#include "ui/rp_widget.h"

namespace HistoryView {

ElementOverlayHost::ElementOverlayHost(
	not_null<QWidget*> container,
	ItemTopFn itemTopFn)
: _container(container)
, _itemTopFn(std::move(itemTopFn)) {
}

void ElementOverlayHost::show(
		not_null<Element*> view,
		FullMsgId context,
		base::unique_qptr<Ui::RpWidget> widget,
		rpl::producer<> closeRequests,
		PositionFn positionFn,
		MediaStateFn mediaStateFn,
		Fn<void()> submitFn) {
	if (_widget && _context == context) {
		hide();
		return;
	}
	hide();

	_view = view;
	_context = context;
	_positionFn = std::move(positionFn);
	_mediaStateFn = std::move(mediaStateFn);
	_submitFn = std::move(submitFn);

	if (_mediaStateFn) {
		_mediaStateFn(view, true);
	}

	_widget = std::move(widget);

	std::move(
		closeRequests
	) | rpl::on_next([=] {
		hide();
	}, _closeLifetime);

	updatePosition();
	_widget->show();
}

void ElementOverlayHost::hide() {
	if (!_widget) {
		return;
	}
	cleanup(true);
}

void ElementOverlayHost::viewGone(not_null<const Element*> view) {
	if (_view != view.get()) {
		return;
	}
	cleanup(false);
}

void ElementOverlayHost::cleanup(bool notifyMedia) {
	if (notifyMedia && _view && _mediaStateFn) {
		_mediaStateFn(_view, false);
	}
	_widget = nullptr;
	_view = nullptr;
	_context = FullMsgId();
	_positionFn = nullptr;
	_mediaStateFn = nullptr;
	_submitFn = nullptr;
	_closeLifetime.destroy();
	if (const auto callback = _hiddenCallback) {
		callback();
	}
}

void ElementOverlayHost::updatePosition() {
	if (!_widget || !_view || !_positionFn) {
		return;
	}
	const auto top = _itemTopFn(_view);
	if (top < 0) {
		return;
	}
	if (!_positionFn(_view, top)) {
		hide();
	}
}

void ElementOverlayHost::handleClickOutside(QPoint clickPos) {
	if (!_widget || !_view) {
		return;
	}
	const auto top = _itemTopFn(_view);
	const auto viewRect = (top >= 0)
		? QRect(0, top, _container->width(), _view->height())
		: QRect();
	if (!viewRect.contains(clickPos)) {
		hide();
	}
}

void ElementOverlayHost::triggerSubmit(FullMsgId context) {
	if (_widget && _context == context && _submitFn) {
		_submitFn();
	}
}

void ElementOverlayHost::setHiddenCallback(Fn<void()> callback) {
	_hiddenCallback = std::move(callback);
}

bool ElementOverlayHost::active() const {
	return _widget != nullptr;
}

FullMsgId ElementOverlayHost::context() const {
	return _context;
}

} // namespace HistoryView
