/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/audio_disc_button.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"

#include "styles/style_audio_disc_button.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kSpinDuration = crl::time(4000);
constexpr auto kFallbackArc = 90 * 16;

} // namespace

AudioDiscButton::AudioDiscButton(
	QWidget *parent,
	FallbackRing fallbackRing)
: RippleButton(parent, st::universalRippleAnimation)
, _fallbackRing(fallbackRing) {
	resize(Size(st::audioDiscSize));
	_spin.init([=](crl::time now) {
		update();
		return _active;
	});
}

AudioDiscButton::~AudioDiscButton() = default;

void AudioDiscButton::setCover(const QImage &cover) {
	if (cover.isNull()) {
		_cover = QImage();
	} else {
		const auto ratio = style::DevicePixelRatio();
		const auto side = st::audioDiscCoverSize * ratio;
		auto scaled = cover.scaled(
			side,
			side,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto crop = QRect(
			(scaled.width() - side) / 2,
			(scaled.height() - side) / 2,
			side,
			side);
		_cover = Images::Circle(scaled.copy(crop));
		_cover.setDevicePixelRatio(ratio);
	}
	update();
}

void AudioDiscButton::setActive(bool active) {
	if (_active == active) {
		return;
	}
	const auto ring = _ringAnimation.value(_active ? 1. : 0.);
	_spinBase = angle();
	_active = active;
	_spinStarted = crl::now();
	if (_active) {
		_spin.start();
	} else {
		_spin.stop();
	}
	if (anim::Disabled()) {
		_ringAnimation.stop();
	} else {
		_ringAnimation.start(
			[=] { update(); },
			ring,
			_active ? 1. : 0.,
			st::audioDiscRingDuration);
	}
	update();
}

float64 AudioDiscButton::angle() const {
	if (!_active) {
		return _spinBase;
	}
	const auto passed = crl::now() - _spinStarted;
	return std::fmod(_spinBase + passed * 360. / kSpinDuration, 360.);
}

void AudioDiscButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto full = QRectF(rect());
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawEllipse(full);
	paintRipple(p, QPoint());
	const auto progress = _ringAnimation.value(_active ? 1. : 0.);
	if (progress > 0.) {
		const auto ring = float64(st::audioDiscRing);
		auto color = st::audioDiscRingFg->c;
		color.setAlphaF(color.alphaF() * progress);
		auto pen = QPen(color);
		pen.setWidthF(ring);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto inset = ring / 2.;
		const auto margins = QMarginsF(inset, inset, inset, inset);
		const auto bounds = full.marginsRemoved(margins);
		if (_cover.isNull()
			&& _fallbackRing == FallbackRing::SpinningArc) {
			p.drawArc(bounds, -int(angle() * 16), kFallbackArc);
		} else {
			p.drawEllipse(bounds);
		}
		p.setPen(Qt::NoPen);
	}
	if (_cover.isNull()) {
		const auto &icon = isOver()
			? st::audioDiscIconOver
			: st::audioDiscIcon;
		icon.paintInCenter(p, rect());
		return;
	}
	const auto cover = float64(st::audioDiscCoverSize);
	p.save();
	p.translate(rect::center(full));
	p.rotate(angle());
	p.drawImage(QRectF(-cover / 2., -cover / 2., cover, cover), _cover);
	p.restore();
}

void AudioDiscButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage AudioDiscButton::prepareRippleMask() const {
	return Images::EllipseMask(size());
}

QPoint AudioDiscButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

} // namespace Ui
