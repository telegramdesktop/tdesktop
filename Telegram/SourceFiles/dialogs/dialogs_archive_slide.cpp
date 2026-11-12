/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_archive_slide.h"

#include "styles/style_basic.h"

namespace Dialogs {

ArchiveSlide::ArchiveSlide(Fn<void()> update)
: _update(std::move(update)) {
}

void ArchiveSlide::start(
		int from,
		int to,
		QImage band,
		rpl::producer<bool> covered) {
	stop();
	_from = from;
	_to = to;
	_band = std::move(band);
	_waiting = true;
	std::move(
		covered
	) | rpl::filter(!rpl::mappers::_1) | rpl::take(1) | rpl::on_next([=] {
		_waiting = false;
		_animation.start([=] {
			if (!_animation.animating()) {
				_band = QImage();
			}
			_update();
		}, _from, _to, st::slideWrapDuration, anim::easeOutCubic);
	}, _lifetime);
}

void ArchiveSlide::stop() {
	_lifetime.destroy();
	_waiting = false;
	_animation.stop();
	_band = QImage();
}

int ArchiveSlide::shift() const {
	return _animation.animating()
		? (int(base::SafeRound(_animation.value(_to))) - _to)
		: _waiting
		? (_from - _to)
		: 0;
}

void ArchiveSlide::paintBand(QPainter &p) const {
	if (!_band.isNull() && (_waiting || _animation.animating())) {
		p.drawImage(0, _to - _from, _band);
	}
}

} // namespace Dialogs
