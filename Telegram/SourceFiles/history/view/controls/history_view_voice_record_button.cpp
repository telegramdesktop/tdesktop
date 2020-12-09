/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_button.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"

#include <QMatrix>

namespace HistoryView::Controls {

namespace {

constexpr auto kSegmentsCount = 12;
constexpr auto kMajorDegreeOffset = 360 / kSegmentsCount;
constexpr auto kSixtyDegrees = 60;

constexpr auto kEnterIdleAnimationDuration = crl::time(1200);

constexpr auto kRotationSpeed = 0.36 * 0.1;

constexpr auto kRandomAdditionFactor = 0.15;

constexpr auto kIdleRadiusGlobalFactor = 0.56;
constexpr auto kIdleRadiusFactor = 0.15 * 0.5;

constexpr auto kOpacityMajor = 0.30;
constexpr auto kOpacityMinor = 0.15;

constexpr auto kIdleRotationSpeed = 0.2;
constexpr auto kIdleRotateDiff = 0.1 * kIdleRotationSpeed;

constexpr auto kWaveAngle = 0.03;

constexpr auto kAnimationSpeedMajor = 1.5 - 0.65;
constexpr auto kAnimationSpeedMinor = 1.5 - 0.45;
constexpr auto kAnimationSpeedCircle = 1.5 - 0.25;

constexpr auto kAmplitudeDiffFactorMax = 500. - 100.;
constexpr auto kAmplitudeDiffFactorMajor = 300. - 100.;
constexpr auto kAmplitudeDiffFactorMinor = 400. - 100.;

constexpr auto kFlingDistanceFactorMajor = 8 * 16;
constexpr auto kFlingDistanceFactorMinor = 20 * 16;

constexpr auto kFlingInAnimationDurationMajor = 200;
constexpr auto kFlingInAnimationDurationMinor = 350;
constexpr auto kFlingOutAnimationDurationMajor = 220;
constexpr auto kFlingOutAnimationDurationMinor = 380;

constexpr auto kSineWaveSpeedMajor = 0.02 * 0.2;
constexpr auto kSineWaveSpeedMinor = 0.026 * 0.2;

constexpr auto kSmallWaveRadius = 0.55;

constexpr auto kFlingDistance = 0.50;

constexpr auto kMinDivider = 100.;

constexpr auto kMaxAmplitude = 1800.;

constexpr auto kZeroPoint = QPointF(0, 0);

template <typename Number>
void Normalize(Number &value, Number right) {
	if (value >= right) {
		value -= right;
	}
}

float64 RandomAdditional() {
	return (rand_value<int>() % 100 / 100.);
}

void PerformAnimation(
		rpl::producer<crl::time> &&animationTicked,
		Fn<void(float64)> &&applyValue,
		Fn<void()> &&finishCallback,
		float64 duration,
		float64 from,
		float64 to,
		rpl::lifetime &lifetime) {
	lifetime.destroy();
	const auto animValue =
		lifetime.make_state<anim::value>(from, to);
	const auto animStarted = crl::now();
	std::move(
		animationTicked
	) | rpl::start_with_next([=,
			applyValue = std::move(applyValue),
			finishCallback = std::move(finishCallback),
			&lifetime](crl::time now) mutable {
		const auto dt = anim::Disabled()
			? 1.
			: ((now - animStarted) / duration);
		if (dt >= 1.) {
			animValue->finish();
			applyValue(animValue->current());
			lifetime.destroy();
			if (finishCallback) {
				finishCallback();
			}
		} else {
			animValue->update(dt, anim::linear);
			applyValue(animValue->current());
		}
	}, lifetime);
}

} // namespace

class ContinuousValue {
public:
	ContinuousValue() = default;
	ContinuousValue(float64 duration) : _duration(duration) {
	}
	void start(float64 to, float64 duration) {
		_to = to;
		_delta = (_to - _cur) / duration;
	}
	void start(float64 to) {
		start(to, _duration);
	}
	void reset() {
		_to = _cur = _delta = 0.;
	}

	float64 current() const {
		return _cur;
	}
	float64 to() const {
		return _to;
	}
	float64 delta() const {
		return _delta;
	}
	void update(crl::time dt, Fn<void(float64 &)> &&callback = nullptr) {
		if (_to != _cur) {
			_cur += _delta * dt;
			if ((_to != _cur) && ((_delta > 0) == (_cur > _to))) {
				_cur = _to;
			}
			if (callback) {
				callback(_cur);
			}
		}
	}

private:
	float64 _duration = 0.;
	float64 _to = 0.;

	float64 _cur = 0.;
	float64 _delta = 0.;

};

class CircleBezier final {
public:
	CircleBezier(int n);

	void computeRandomAdditionals();
	void paintCircle(
		Painter &p,
		const QColor &c,
		float64 radius,
		float64 cubicBezierFactor,
		float64 idleStateDiff,
		float64 radiusDiff,
		float64 randomFactor);

private:
	struct Points {
		QPointF point;
		QPointF control;
	};

	const int _segmentsCount;
	const float64 _segmentLength;
	std::vector<float64> _randomAdditionals;

};

class Wave final {
public:
	Wave(
		rpl::producer<crl::time> animationTicked,
		int n,
		float64 rotationOffset,
		float64 amplitudeRadius,
		float64 amplitudeWaveDiff,
		float64 fling,
		int flingDistanceFactor,
		int flingInAnimationDuration,
		int flingOutAnimationDuration,
		float64 amplitudeDiffSpeed,
		float64 amplitudeDiffFactor,
		bool isDirectionClockwise);

	void setValue(float64 to);
	void tick(float64 circleRadius, crl::time dt);
	void reset();

	void paint(Painter &p, QColor c);

private:

	void initEnterIdleAnimation(rpl::producer<crl::time> animationTicked);
	void initFlingAnimation(rpl::producer<crl::time> animationTicked);

	const std::unique_ptr<CircleBezier> _circleBezier;

	const float _rotationOffset;
	const float64 _idleGlobalRadius;
	const float64 _amplitudeRadius;
	const float64 _amplitudeWaveDiff;
	const float64 _randomAdditions;
	const float64 _fling;
	const int _flingDistanceFactor;
	const int _flingInAnimationDuration;
	const int _flingOutAnimationDuration;
	const float64 _amplitudeInAnimationDuration;
	const float64 _amplitudeOutAnimationDuration;
	const int _directionClockwise;

	bool _incRandomAdditionals = false;
	bool _isIdle = true;
	bool _wasFling = false;
	float64 _flingRadius = 0.;
	float64 _idleRadius = 0.;
	float64 _idleRotation = 0.;
	float64 _lastRadius = 0.;
	float64 _rotation = 0.;
	float64 _sineAngleMax = 0.;
	float64 _waveAngle = 0.;
	float64 _waveDiff = 0.;
	ContinuousValue _levelValue;

	rpl::event_stream<float64> _flingAnimationRequests;
	rpl::event_stream<> _enterIdleAnimationRequests;
	rpl::lifetime _animationEnterIdleLifetime;
	rpl::lifetime _animationFlingLifetime;
	rpl::lifetime _lifetime;
};

class RecordCircle final {
public:
	RecordCircle(rpl::producer<crl::time> animationTicked);

	void reset();
	void setAmplitude(float64 value);
	void paint(Painter &p, QColor c);

private:

	const std::unique_ptr<Wave> _majorWave;
	const std::unique_ptr<Wave> _minorWave;

	crl::time _lastUpdateTime = 0;
	ContinuousValue _levelValue;

};

CircleBezier::CircleBezier(int n)
: _segmentsCount(n)
, _segmentLength((4.0 / 3.0) * std::tan(M_PI / (2 * n)))
, _randomAdditionals(n) {
}

void CircleBezier::computeRandomAdditionals() {
	ranges::generate(_randomAdditionals, RandomAdditional);
}

void CircleBezier::paintCircle(
		Painter &p,
		const QColor &c,
		float64 radius,
		float64 cubicBezierFactor,
		float64 idleStateDiff,
		float64 radiusDiff,
		float64 randomFactor) {
	PainterHighQualityEnabler hq(p);

	const auto r1 = radius - idleStateDiff / 2. - radiusDiff / 2.;
	const auto r2 = radius + radiusDiff / 2. + idleStateDiff / 2.;
	const auto l = _segmentLength * std::max(r1, r2) * cubicBezierFactor;

	auto m = QMatrix();

	const auto preparePoints = [&](int i, bool isStart) -> Points {
		Normalize(i, _segmentsCount);
		const auto randomAddition = randomFactor * _randomAdditionals[i];
		const auto r = ((i % 2 == 0) ? r1 : r2) + randomAddition;

		m.reset();
		m.rotate(360. / _segmentsCount * i);
		const auto sign = isStart ? 1 : -1;

		return {
			(isStart && i) ? QPointF() : m.map(QPointF(0, -r)),
			m.map(QPointF(sign * (l + randomAddition * _segmentLength), -r)),
		};
	};

	const auto &[startPoint, _] = preparePoints(0, true);

	auto path = QPainterPath();
	path.moveTo(startPoint);

	for (auto i = 0; i < _segmentsCount; i++) {
		const auto &[_, startControl] = preparePoints(i, true);
		const auto &[end, endControl] = preparePoints(i + 1, false);

		path.cubicTo(startControl, endControl, end);
	}

	p.setBrush(Qt::NoBrush);

	auto pen = QPen(Qt::NoPen);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);

	p.setPen(pen);
	p.fillPath(path, c);
	p.drawPath(path);
}

Wave::Wave(
	rpl::producer<crl::time> animationTicked,
	int n,
	float64 rotationOffset,
	float64 amplitudeRadius,
	float64 amplitudeWaveDiff,
	float64 fling,
	int flingDistanceFactor,
	int flingInAnimationDuration,
	int flingOutAnimationDuration,
	float64 amplitudeDiffSpeed,
	float64 amplitudeDiffFactor,
	bool isDirectionClockwise)
: _circleBezier(std::make_unique<CircleBezier>(n))
, _rotationOffset(rotationOffset)
, _idleGlobalRadius(st::historyRecordRadiusDiffMin * kIdleRadiusGlobalFactor)
, _amplitudeRadius(amplitudeRadius)
, _amplitudeWaveDiff(amplitudeWaveDiff)
, _randomAdditions(st::historyRecordRandomAddition * kRandomAdditionFactor)
, _fling(fling)
, _flingDistanceFactor(flingDistanceFactor)
, _flingInAnimationDuration(flingInAnimationDuration)
, _flingOutAnimationDuration(flingOutAnimationDuration)
, _amplitudeInAnimationDuration(kMinDivider
	+ amplitudeDiffFactor * amplitudeDiffSpeed)
, _amplitudeOutAnimationDuration(kMinDivider
	+ kAmplitudeDiffFactorMax * amplitudeDiffSpeed)
, _directionClockwise(isDirectionClockwise ? 1 : -1)
, _rotation(rotationOffset) {
	initEnterIdleAnimation(rpl::duplicate(animationTicked));
	initFlingAnimation(std::move(animationTicked));
}

void Wave::reset() {
	_incRandomAdditionals = false;
	_isIdle = true;
	_wasFling = false;
	_flingRadius = 0.;
	_idleRadius = 0.;
	_idleRotation = 0.;
	_lastRadius = 0.;
	_rotation = 0.;
	_sineAngleMax = 0.;
	_waveAngle = 0.;
	_waveDiff = 0.;
	_levelValue.reset();
}

void Wave::setValue(float64 to) {
	const auto duration = (to <= _levelValue.current())
		? _amplitudeOutAnimationDuration
		: _amplitudeInAnimationDuration;
	_levelValue.start(to, duration);

	const auto idle = to < 0.1;
	if (_isIdle != idle && idle) {
		_enterIdleAnimationRequests.fire({});
	}

	_isIdle = idle;

	if (!_isIdle) {
		_animationEnterIdleLifetime.destroy();
	}
}

void Wave::initEnterIdleAnimation(rpl::producer<crl::time> animationTicked) {
	_enterIdleAnimationRequests.events(
	) | rpl::start_with_next([=] {
		const auto &k = kSixtyDegrees;

		const auto rotation = _rotation;
		const auto rotationTo = std::round(rotation / k) * k
			+ _rotationOffset;
		const auto waveDiff = _waveDiff;

		auto applyValue = [=](float64 v) {
			_rotation = rotationTo + (rotation - rotationTo) * v;
			_waveDiff = 1. + (waveDiff - 1.) * v;
			_waveAngle = std::acos(_waveDiff * _directionClockwise);
		};

		PerformAnimation(
			rpl::duplicate(animationTicked),
			std::move(applyValue),
			nullptr,
			kEnterIdleAnimationDuration,
			1,
			0,
			_animationEnterIdleLifetime);

	}, _lifetime);
}

void Wave::initFlingAnimation(rpl::producer<crl::time> animationTicked) {
	_flingAnimationRequests.events(
	) | rpl::start_with_next([=](float64 delta) {

		const auto fling = _fling * 2;
		const auto flingDistance = delta
			* _amplitudeRadius
			* _flingDistanceFactor
			* fling;

		const auto applyValue = [=](float64 v) {
			_flingRadius = v;
		};
		auto finishCallback = [=] {
			PerformAnimation(
				rpl::duplicate(animationTicked),
				applyValue,
				nullptr,
				_flingOutAnimationDuration * fling,
				flingDistance,
				0,
				_animationFlingLifetime);
		};

		PerformAnimation(
			rpl::duplicate(animationTicked),
			applyValue,
			std::move(finishCallback),
			_flingInAnimationDuration * fling,
			_flingRadius,
			flingDistance,
			_animationFlingLifetime);

	}, _lifetime);
}

void Wave::tick(float64 circleRadius, crl::time dt) {

	auto amplitudeCallback = [&](float64 &value) {
		if (std::abs(value - _levelValue.to()) * _amplitudeRadius
				< (st::historyRecordRandomAddition / 2)) {
			if (!_wasFling) {
				_flingAnimationRequests.fire_copy(_levelValue.delta());
				_wasFling = true;
			}
		} else {
			_wasFling = false;
		}
	};
	_levelValue.update(dt, std::move(amplitudeCallback));

	_idleRadius = circleRadius * kIdleRadiusFactor;

	{
		const auto to = _levelValue.to();
		const auto delta = (_sineAngleMax - to);
		if (std::abs(delta) - 0.25 < 0) {
			_sineAngleMax = to;
		} else {
			_sineAngleMax -= 0.25 * ((delta < 0) ? -1 : 1);
		}
	}

	if (!_isIdle) {
		_rotation += dt
			* (kRotationSpeed * 4. * std::min(_levelValue.current() / .5, 1.)
				+ kRotationSpeed * 0.5);
		Normalize(_rotation, 360.);
	} else {
		_idleRotation += kIdleRotateDiff * dt;
		Normalize(_idleRotation, 360.);
	}

	_lastRadius = circleRadius;

	if (!_isIdle) {
		_waveAngle += (_amplitudeWaveDiff * _sineAngleMax) * dt;
		_waveDiff = std::cos(_waveAngle) * _directionClockwise;

		if ((_waveDiff != 0) && ((_waveDiff > 0) == _incRandomAdditionals)) {
			_circleBezier->computeRandomAdditionals();
			_incRandomAdditionals = !_incRandomAdditionals;
		}
	}
}


void Wave::paint(Painter &p, QColor c) {
	const auto amplitude = _levelValue.current();
	const auto waveAmplitude = std::min(amplitude / .3, 1.);
	const auto radiusDiff = st::historyRecordRadiusDiffMin
		+ st::historyRecordRadiusDiff * kWaveAngle * _levelValue.to();

	const auto diffFactor = 0.35 * waveAmplitude * _waveDiff;

	const auto radius = (_lastRadius + _amplitudeRadius * amplitude)
		+ _idleGlobalRadius
		+ (_flingRadius * waveAmplitude);

	const auto cubicBezierFactor = 1.
		+ std::abs(diffFactor) * waveAmplitude
		+ (1. - waveAmplitude) * kIdleRadiusFactor;

	const auto circleRadiusDiff = std::max(
		radiusDiff * diffFactor,
		st::historyRecordLevelMainRadius - radius);

	p.rotate((_rotation + _idleRotation) * _directionClockwise);

	_circleBezier->paintCircle(
		p,
		c,
		radius,
		cubicBezierFactor,
		_idleRadius * (1. - waveAmplitude),
		circleRadiusDiff,
		waveAmplitude * _waveDiff * _randomAdditions);

	p.rotate(0);
}

RecordCircle::RecordCircle(rpl::producer<crl::time> animationTicked)
: _majorWave(std::make_unique<Wave>(
	rpl::duplicate(animationTicked),
	kSegmentsCount,
	kMajorDegreeOffset,
	st::historyRecordMajorAmplitudeRadius,
	kSineWaveSpeedMajor,
	0.,
	kFlingDistanceFactorMajor,
	kFlingInAnimationDurationMajor,
	kFlingOutAnimationDurationMajor,
	kAnimationSpeedMajor,
	kAmplitudeDiffFactorMajor,
	true))
, _minorWave(std::make_unique<Wave>(
	std::move(animationTicked),
	kSegmentsCount,
	0,
	st::historyRecordMinorAmplitudeRadius
		+ st::historyRecordMinorAmplitudeRadius * kSmallWaveRadius,
	kSineWaveSpeedMinor,
	kFlingDistance,
	kFlingDistanceFactorMinor,
	kFlingInAnimationDurationMinor,
	kFlingOutAnimationDurationMinor,
	kAnimationSpeedMinor,
	kAmplitudeDiffFactorMinor,
	false))
, _levelValue(kMinDivider
	+ kAmplitudeDiffFactorMax * kAnimationSpeedCircle) {
}

void RecordCircle::reset() {
	_majorWave->reset();
	_minorWave->reset();
	_levelValue.reset();
}

void RecordCircle::setAmplitude(float64 value) {
	const auto to = std::min(kMaxAmplitude, value) / kMaxAmplitude;
	_levelValue.start(to);
	_majorWave->setValue(to);
	_minorWave->setValue(to);
}

void RecordCircle::paint(Painter &p, QColor c) {
	const auto dt = crl::now() - _lastUpdateTime;
	_levelValue.update(dt);

	const auto &mainRadius = st::historyRecordLevelMainRadiusAmplitude;
	const auto radius = (st::historyRecordLevelMainRadius
		+ (anim::Disabled() ? 0 : mainRadius * _levelValue.current()));

	if (!anim::Disabled()) {
		_majorWave->tick(radius, dt);
		_minorWave->tick(radius, dt);
		_lastUpdateTime = crl::now();

		const auto opacity = p.opacity();
		p.setOpacity(kOpacityMajor);
		_majorWave->paint(p, c);
		p.setOpacity(kOpacityMinor);
		_minorWave->paint(p, c);
		p.setOpacity(opacity);
	}

	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawEllipse(kZeroPoint, radius, radius);
}

VoiceRecordButton::VoiceRecordButton(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<> leaveWindowEventProducer)
: AbstractButton(parent)
, _recordCircle(std::make_unique<RecordCircle>(
	_recordAnimationTicked.events()))
, _center(st::historyRecordLevelMaxRadius)
, _recordingAnimation([=](crl::time now) {
	if (!anim::Disabled()) {
		update();
	}
	_recordAnimationTicked.fire_copy(now);
	return true;
}) {
	const auto h = st::historyRecordLevelMaxRadius * 2;
	resize(h, h);
	std::move(
		leaveWindowEventProducer
	) | rpl::start_with_next([=] {
		_inCircle = false;
	}, lifetime());
	init();
}

VoiceRecordButton::~VoiceRecordButton() = default;

void VoiceRecordButton::requestPaintLevel(quint16 level) {
	_recordCircle->setAmplitude(level);
	update();
}

void VoiceRecordButton::init() {
	const auto hasProgress = [](auto value) { return value != 0.; };

	const auto stateChangedAnimation =
		lifetime().make_state<Ui::Animations::Simple>();
	const auto currentState = lifetime().make_state<Type>(_state.current());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto progress = _showProgress.current();
		const auto complete = (progress == 1.);

		p.translate(_center, _center);
		if (!complete) {
			p.scale(progress, progress);
		}
		PainterHighQualityEnabler hq(p);
		const auto color = anim::color(
			st::historyRecordVoiceFgInactive,
			st::historyRecordVoiceFgActive,
			_colorProgress.current());
		_recordCircle->paint(p, color);
		p.resetTransform();

		if (!complete) {
			p.setOpacity(progress);
		}

		// Paint icon.
		{
			const auto stateProgress = stateChangedAnimation->value(0.);
			const auto scale = (std::cos(M_PI * 2 * stateProgress) + 1.) * .5;
			p.translate(_center, _center);
			if (scale < 1.) {
				p.scale(scale, scale);
			}
			const auto state = *currentState;
			const auto icon = (state == Type::Send)
				? st::historySendIcon
				: st::historyRecordVoiceActive;
			const auto position = (state == Type::Send)
				? st::historyRecordSendIconPosition
				: QPoint(0, 0);
			icon.paint(
				p,
				-icon.width() / 2 + position.x(),
				-icon.height() / 2 + position.y(),
				0,
				st::historyRecordVoiceFgActiveIcon->c);
		}
	}, lifetime());

	rpl::merge(
		shownValue(),
		_showProgress.value(
		) | rpl::map(hasProgress) | rpl::distinct_until_changed()
	) | rpl::start_with_next([=](bool show) {
		setVisible(show);
		setMouseTracking(show);
		if (!show) {
			_recordingAnimation.stop();
			_showProgress = 0.;
			_recordCircle->reset();
			_state = Type::Record;
		} else {
			if (!_recordingAnimation.animating()) {
				_recordingAnimation.start();
			}
		}
	}, lifetime());

	actives(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool active) {
		setPointerCursor(active);
	}, lifetime());

	_state.changes(
	) | rpl::start_with_next([=](Type newState) {
		const auto to = 1.;
		auto callback = [=](float64 value) {
			if (value >= (to * .5)) {
				*currentState = newState;
			}
			update();
		};
		const auto duration = st::historyRecordVoiceDuration * 2;
		stateChangedAnimation->start(std::move(callback), 0., to, duration);
	}, lifetime());
}

rpl::producer<bool> VoiceRecordButton::actives() const {
	return events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseMove
			|| e->type() == QEvent::Leave
			|| e->type() == QEvent::Enter);
	}) | rpl::map([=](not_null<QEvent*> e) {
		switch(e->type()) {
		case QEvent::MouseMove:
			return inCircle((static_cast<QMouseEvent*>(e.get()))->pos());
		case QEvent::Leave: return false;
		case QEvent::Enter: return inCircle(mapFromGlobal(QCursor::pos()));
		default: return false;
		}
	});
}

bool VoiceRecordButton::inCircle(const QPoint &localPos) const {
	const auto &radii = st::historyRecordLevelMaxRadius;
	const auto dx = std::abs(localPos.x() - _center);
	if (dx > radii) {
		return false;
	}
	const auto dy = std::abs(localPos.y() - _center);
	if (dy > radii) {
		return false;
	} else if (dx + dy <= radii) {
		return true;
	}
	return ((dx * dx + dy * dy) <= (radii * radii));
}

void VoiceRecordButton::requestPaintProgress(float64 progress) {
	_showProgress = progress;
	update();
}

void VoiceRecordButton::requestPaintColor(float64 progress) {
	_colorProgress = progress;
	update();
}

void VoiceRecordButton::setType(Type state) {
	_state = state;
}

} // namespace HistoryView::Controls
