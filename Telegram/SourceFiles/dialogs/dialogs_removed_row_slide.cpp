/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_removed_row_slide.h"

#include "styles/style_basic.h"

namespace Dialogs {

RemovedRowSlide::RemovedRowSlide(Fn<void()> update)
: _update(std::move(update)) {
}

void RemovedRowSlide::block(not_null<Entry*> entry) {
	_blocked = entry;
	if (_pending.entry == entry) {
		_pending = Pending();
	}
}

void RemovedRowSlide::prepare(
		not_null<Entry*> entry,
		not_null<const IndexedList*> list,
		int top,
		QImage image) {
	if (_blocked == entry) {
		return;
	}
	_pending = {
		.entry = entry,
		.list = list,
		.top = top,
		.image = std::move(image),
	};
}

void RemovedRowSlide::start(not_null<Entry*> entry) {
	if (_pending.entry != entry) {
		return;
	}
	_shown = base::take(_pending);
	const auto height = int(_shown.image.height()
		/ _shown.image.devicePixelRatio());
	_animation.stop();
	_animation.start([=] {
		if (!_animation.animating()) {
			_shown = Pending();
		}
		_update();
	}, height, 0., st::slideWrapDuration, anim::easeOutCubic);
}

void RemovedRowSlide::clearPending() {
	_pending = Pending();
	_blocked = nullptr;
}

int RemovedRowSlide::top() const {
	return _shown.top;
}

int RemovedRowSlide::height(not_null<const IndexedList*> list) const {
	return (_shown.list == list && _animation.animating())
		? int(base::SafeRound(_animation.value(0.)))
		: 0;
}

void RemovedRowSlide::paint(QPainter &p, int width) const {
	const auto now = height(_shown.list);
	if (!now) {
		return;
	}
	const auto ratio = _shown.image.devicePixelRatio();
	const auto full = _shown.image.height();
	const auto visible = int(base::SafeRound(now * ratio));
	p.drawImage(
		QRect(0, 0, width, now),
		_shown.image,
		QRect(0, full - visible, _shown.image.width(), visible));
}

} // namespace Dialogs
