/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_story_upload_cancel.h"

#include "ui/painter.h"

#include "styles/style_basic.h"
#include "styles/style_userpic_button.h"

namespace Dialogs::Stories {

UploadCancelOverlay::UploadCancelOverlay(Fn<void()> update)
: _update(std::move(update)) {
}

void UploadCancelOverlay::setHovered(uint64 id) {
	if (_hovered == id) {
		return;
	}
	const auto replace = id && id != _painted;
	const auto from = replace ? 0. : _fade.value(_hovered ? 1. : 0.);
	if (replace) {
		_fade.stop();
		_painted = id;
	}
	_hovered = id;
	_fade.start(_update, from, id ? 1. : 0., st::universalDuration);
}

uint64 UploadCancelOverlay::hovered() const {
	return _hovered;
}

void UploadCancelOverlay::paint(
		QPainter &p,
		uint64 id,
		const QRectF &rect) {
	if (!id || id != _painted) {
		return;
	}
	const auto opacity = _fade.value(_hovered ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.setPen(Qt::NoPen);
	p.setBrush(st::songCoverOverlayFg);
	p.drawEllipse(rect);
	st::userpicUploadCancel.paintInCenter(p, rect.toAlignedRect());
	p.restore();
}

} // namespace Dialogs::Stories
