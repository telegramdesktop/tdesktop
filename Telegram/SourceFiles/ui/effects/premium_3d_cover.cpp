/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_3d_cover.h"

#include "ui/effects/premium_3d_support.h"
#include "ui/gl/gl_surface.h"
#include "base/random.h"
#include "base/debug_log.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QRhiWidget>
#endif // Qt >= 6.7

namespace Ui::Premium {
namespace {

constexpr auto kDragYaw = 0.5;
constexpr auto kDragPitch = 0.05;
constexpr auto kMaxFrameDelta = crl::time(66);
constexpr auto kMsInSecond = 1000.;
constexpr auto kBackDuration = crl::time(600);
constexpr auto kTapInDuration = crl::time(220);
constexpr auto kTapBase = 40;
constexpr auto kTapRandom = 30;
constexpr auto kTapMaxMove = 4;
constexpr auto kOvershootTension = 2.;

} // namespace

Object3dCover::Object3dCover(QWidget *parent, Descriptor descriptor)
: RpWidget(parent)
, _descriptor(descriptor)
, _animation([=] { frame(); }) {
}

Object3dCover::~Object3dCover() = default;

bool Object3dCover::Supported() {
	return Object3dSupported();
}

void Object3dCover::setShownProgress(float64 progress) {
	progress = std::clamp(progress, 0., 1.);
	if (_opacity == progress) {
		return;
	}
	_opacity = progress;
	if (!_animation.animating()) {
		pushState();
		if (const auto w = surfaceWidget()) {
			w->update();
		}
	}
}

void Object3dCover::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (_paused) {
		freeze();
	} else {
		unfreeze();
	}
}

void Object3dCover::freeze() {
	stopAnimation();
	_pausedAt = crl::now();
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	const auto w = surfaceWidget();
	if (!w || w->isHidden()) {
		return;
	}
	const auto rhi = qobject_cast<QRhiWidget*>(w);
	auto image = rhi ? rhi->grabFramebuffer() : QImage();
	if (image.isNull()) {
		return;
	}
	_frozen = std::move(image);
	pushState();
	w->update();
	update();
#endif // Qt >= 6.7
}

void Object3dCover::unfreeze() {
	if (!_frozen.isNull()) {
		_frozen = QImage();
		update();
	}
	if (_pausedAt) {
		const auto delta = crl::now() - _pausedAt;
		if (_gesture) {
			_gesture->start += delta;
		}
		if (_idleAt) {
			_idleAt += delta;
		}
		_pausedAt = 0;
	}
	if (!isHidden()) {
		startAnimation();
	}
}

void Object3dCover::setNight(bool night) {
	if (_night == night) {
		return;
	}
	_night = night;
	if (!_animation.animating()) {
		pushState();
		if (const auto w = surfaceWidget()) {
			w->update();
		}
	}
}

rpl::producer<float64> Object3dCover::flungStrength() const {
	return _flung.events();
}

QWidget *Object3dCover::surfaceWidget() const {
	return _surface ? _surface->rpWidget() : nullptr;
}

void Object3dCover::ensureSurface() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_surface) {
		return;
	}
	_surface = GL::CreateSurface(
		this,
		GL::ChosenRenderer{
			.renderer = createRenderer(),
			.backend = GL::Backend::QRhi,
		});
	if (const auto w = surfaceWidget()) {
		if (const auto rhiWidget = qobject_cast<QRhiWidget*>(w)) {
			rhiWidget->setSampleCount(_descriptor.sampleCount);
		} else {
			LOG(("Premium3dCover: surface is not a QRhiWidget, skip MSAA."));
		}
		w->setAttribute(Qt::WA_TransparentForMouseEvents);
		w->setAttribute(Qt::WA_AlwaysStackOnTop);
		w->setGeometry(rect());
		w->hide();
	}
#endif // Qt >= 6.7
}

void Object3dCover::startAnimation() {
	if (_paused || _animation.animating()) {
		return;
	}
	_lastFrame = crl::now();
	_animation.start();
}

void Object3dCover::stopAnimation() {
	_animation.stop();
}

void Object3dCover::advance(float64 dt) {
	_time += dt;
	_time -= _descriptor.period * std::floor(_time / _descriptor.period);
}

void Object3dCover::applyChannel(Channel channel, float64 value) {
	switch (channel) {
	case Channel::Yaw: _yaw = value; break;
	case Channel::Pitch: _pitch = value; break;
	}
}

void Object3dCover::tick(crl::time now) {
	const auto ease = [](Easing easing, float64 t) -> float64 {
		switch (easing) {
		case Easing::Linear:
			return t;
		case Easing::EaseOutQuint:
			return CubicBezier(0.23, 1., 0.32, 1., t);
		case Easing::Overshoot: {
			const auto u = t - 1.;
			return u * u * ((kOvershootTension + 1.) * u + kOvershootTension)
				+ 1.;
		}
		}
		return t;
	};
	const auto valueAt = [](const Track &track, float64 eased) -> float64 {
		const auto last = int(track.values.size()) - 1;
		if (last <= 0) {
			return track.values.empty() ? 0. : track.values.front();
		} else if (last == 1) {
			return track.values[0]
				+ eased * (track.values[1] - track.values[0]);
		}
		const auto position = eased * last;
		const auto segment = std::clamp(
			int(std::floor(position)),
			0,
			last - 1);
		const auto local = position - segment;
		return track.values[segment]
			+ local * (track.values[segment + 1] - track.values[segment]);
	};

	if (_gesture) {
		const auto elapsed = now - _gesture->start;
		for (const auto &track : _gesture->tracks) {
			if (elapsed < track.delay) {
				continue;
			}
			const auto local = std::min(
				elapsed - track.delay,
				track.duration);
			const auto fraction = (track.duration > 0)
				? (local / float64(track.duration))
				: 1.;
			applyChannel(
				track.channel,
				valueAt(track, ease(track.easing, fraction)));
		}
		if (elapsed >= _gesture->total) {
			_yaw = 0.;
			_gesture.reset();
			scheduleIdle(_descriptor.respinDelay);
		}
	} else if (_idleAt && now >= _idleAt && !_dragging) {
		_idleAt = 0;
		startSpin();
	}
}

void Object3dCover::pushState() {
	applyState(
		float(_yaw),
		float(_pitch),
		float(_time),
		_paused ? 0.f : float(_opacity),
		_night);
}

void Object3dCover::frame() {
	const auto now = crl::now();
	const auto delta = std::clamp(
		now - _lastFrame,
		crl::time(1),
		kMaxFrameDelta);
	_lastFrame = now;
	advance(delta / kMsInSecond);
	tick(now);
	pushState();
	if (const auto w = surfaceWidget()) {
		w->update();
	}
}

void Object3dCover::play(Gesture gesture) {
	gesture.start = crl::now();
	gesture.total = 0;
	for (const auto &track : gesture.tracks) {
		gesture.total = std::max(gesture.total, track.delay + track.duration);
	}
	cancelIdle();
	_gesture = std::move(gesture);
}

void Object3dCover::scheduleIdle(crl::time delay) {
	_idleAt = crl::now() + delay;
}

void Object3dCover::cancelIdle() {
	_idleAt = 0;
}

void Object3dCover::startSpin() {
	auto gesture = Gesture();
	gesture.tracks.push_back({
		.channel = Channel::Yaw,
		.values = { 0., 360. },
		.duration = _descriptor.spinDuration,
		.easing = _descriptor.spinEaseOut
			? Easing::EaseOutQuint
			: Easing::Linear,
	});
	play(std::move(gesture));
}

void Object3dCover::startBackSpring() {
	const auto fromYaw = _yaw;
	const auto fromPitch = _pitch;
	auto gesture = Gesture();
	gesture.tracks.push_back({
		.channel = Channel::Yaw,
		.values = { fromYaw, 0. },
		.duration = kBackDuration,
		.easing = Easing::Overshoot,
	});
	gesture.tracks.push_back({
		.channel = Channel::Pitch,
		.values = { fromPitch, 0. },
		.duration = kBackDuration,
		.easing = Easing::Overshoot,
	});
	play(std::move(gesture));
	const auto magnitude = std::abs(fromYaw + fromPitch);
	if (magnitude > 0.) {
		_flung.fire_copy(magnitude);
	}
	scheduleIdle(_descriptor.respinDelay);
}

void Object3dCover::startTapTilt(QPoint position) {
	const auto radius = std::max(width(), 1) / 2.;
	const auto toYaw = (kTapBase + base::RandomIndex(kTapRandom))
		* (radius - position.x()) / radius;
	const auto toPitch = (kTapBase + base::RandomIndex(kTapRandom))
		* (radius - position.y()) / radius;
	auto gesture = Gesture();
	gesture.tracks.push_back({
		.channel = Channel::Yaw,
		.values = { 0., toYaw },
		.duration = kTapInDuration,
		.easing = Easing::EaseOutQuint,
	});
	gesture.tracks.push_back({
		.channel = Channel::Yaw,
		.values = { toYaw, 0. },
		.delay = kTapInDuration,
		.duration = kBackDuration,
		.easing = Easing::Overshoot,
	});
	gesture.tracks.push_back({
		.channel = Channel::Pitch,
		.values = { 0., toPitch },
		.duration = kTapInDuration,
		.easing = Easing::EaseOutQuint,
	});
	gesture.tracks.push_back({
		.channel = Channel::Pitch,
		.values = { toPitch, 0. },
		.delay = kTapInDuration,
		.duration = kBackDuration,
		.easing = Easing::Overshoot,
	});
	play(std::move(gesture));
}

void Object3dCover::paintEvent(QPaintEvent *e) {
	if (_frozen.isNull()) {
		return;
	}
	auto p = QPainter(this);
	p.drawImage(rect(), _frozen);
}

void Object3dCover::resizeEvent(QResizeEvent *e) {
	if (const auto w = surfaceWidget()) {
		w->setGeometry(rect());
	}
}

void Object3dCover::showEvent(QShowEvent *e) {
	ensureSurface();
	if (const auto w = surfaceWidget()) {
		w->show();
	}
	if (!_entered) {
		_entered = true;
		_yaw = _pitch = 0.;
		_gesture.reset();
		scheduleIdle(0);
	}
	startAnimation();
}

void Object3dCover::hideEvent(QHideEvent *e) {
	stopAnimation();
	if (_entered) {
		return;
	}
	_gesture.reset();
	cancelIdle();
	_yaw = _pitch = 0.;
}

void Object3dCover::mousePressEvent(QMouseEvent *e) {
	_dragging = true;
	_moved = false;
	_pressPos = e->pos();
	_lastDragPos = e->pos();
	_gesture.reset();
	cancelIdle();
}

void Object3dCover::mouseMoveEvent(QMouseEvent *e) {
	if (!_dragging) {
		return;
	}
	const auto pos = e->pos();
	if ((pos - _pressPos).manhattanLength() > kTapMaxMove) {
		_moved = true;
	}
	const auto shift = pos - _lastDragPos;
	_yaw += -shift.x() * kDragYaw;
	_pitch += -shift.y() * kDragPitch;
	_lastDragPos = pos;
}

void Object3dCover::mouseReleaseEvent(QMouseEvent *e) {
	if (!_dragging) {
		return;
	}
	_dragging = false;
	if (_moved) {
		startBackSpring();
	} else {
		startTapTilt(_pressPos);
	}
}

} // namespace Ui::Premium
