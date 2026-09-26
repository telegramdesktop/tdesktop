/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/upload_progress_overlay.h"

#include "base/call_delayed.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

namespace Ui {

UploadProgressOverlay::UploadProgressOverlay(
	not_null<QWidget*> parent,
	Fn<void()> update)
: _parent(parent)
, _update(std::move(update))
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
}

void UploadProgressOverlay::start() {
	_uploading = true;
	_stopping = false;
	_startedAt = crl::now();
	_progress = 0.;
	_hiding.stop();
	_hidingDone = nullptr;
	_radial.start(_progress);
	_update();
}

void UploadProgressOverlay::stop(Fn<void()> done) {
	if (!_uploading) {
		if (done) {
			done();
		}
		return;
	}
	_hidingDone = std::move(done);
	_stopping = true;
	_progress = 1.;
	_update();

	const auto catchUp = st::radialDuration * 2;
	base::call_delayed(catchUp, _parent, [=] {
		if (!_stopping) {
			return;
		}
		proceedWithStop();
	});
}

void UploadProgressOverlay::fail(Fn<void()> done) {
	if (!_uploading) {
		if (done) {
			done();
		}
		return;
	}
	_hidingDone = std::move(done);
	_stopping = true;
	_progress = 0.;
	_update();

	const auto catchUp = st::radialDuration * 2;
	base::call_delayed(catchUp, _parent, [=] {
		if (!_stopping) {
			return;
		}
		proceedWithStop();
	});
}

void UploadProgressOverlay::proceedWithStop() {
	_stopping = false;
	_uploading = false;

	const auto elapsed = crl::now() - _startedAt;
	const auto minDuration = st::radialDuration * 3;
	const auto delay = std::max(minDuration - elapsed, crl::time(0));
	base::call_delayed(delay, _parent, [=] {
		if (_uploading) {
			return;
		}
		_hiding.start(
			_update,
			1.,
			0.,
			st::radialDuration);
		_hiding.setFinishedCallback([=] {
			_radial.stop();
			const auto callback = base::take(_hidingDone);
			_update();
			if (callback) {
				callback();
			}
		});
	});
}

void UploadProgressOverlay::finish(Fn<void()> done) {
	_uploading = false;
	_update();
	if (done) {
		done();
	}
}

void UploadProgressOverlay::setProgress(float64 progress) {
	_progress = progress;
	_update();
}

void UploadProgressOverlay::setOver(bool over) {
	_over = over;
	if (!_uploading) {
		return;
	}
	_cancelShown.start(
		_update,
		over ? 0. : 1.,
		over ? 1. : 0.,
		st::universalDuration);
}

bool UploadProgressOverlay::shown() const {
	return _uploading
		|| _radial.animating()
		|| _hiding.animating()
		|| _hidingDone;
}

bool UploadProgressOverlay::uploading() const {
	return _uploading && !_stopping;
}

void UploadProgressOverlay::paint(
		QPainter &p,
		QRect rect,
		const PaintArgs &args) {
	const auto hideOpacity = _hiding.animating()
		? _hiding.value(0.)
		: (_uploading || _hidingDone) ? 1. : 0.;
	if (hideOpacity <= 0.) {
		return;
	}

	auto o = p.opacity();
	if (hideOpacity < 1.) {
		p.setOpacity(o * hideOpacity);
	}

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(args.overlayFg);
	if (args.roundRadius > 0.) {
		p.drawRoundedRect(rect, args.roundRadius, args.roundRadius);
	} else {
		p.drawEllipse(rect);
	}

	if (_uploading) {
		const auto cancelOpacity = _cancelShown.value(
			_over ? 1. : 0.);
		if (cancelOpacity > 0. && args.cancelIcon) {
			p.setOpacity(o * hideOpacity * cancelOpacity);
			args.cancelIcon->paintInCenter(p, rect);
		}
	}

	p.setOpacity(o * hideOpacity);
	const auto line = float64(args.lineWidth);
	const auto margin = float64(args.margin);
	const auto arc = QRectF(rect) - QMarginsF(
		margin,
		margin,
		margin,
		margin);
	_radial.draw(p, arc, line, args.progressFg);

	p.setOpacity(o);
}

void UploadProgressOverlay::radialAnimationCallback(crl::time now) {
	const auto updated = _radial.update(
		_progress,
		!_uploading,
		now);
	if (!anim::Disabled() || updated || _radial.animating()) {
		_update();
	}
}

} // namespace Ui
