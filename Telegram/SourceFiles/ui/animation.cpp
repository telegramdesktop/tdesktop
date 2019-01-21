/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "animation.h"

#include "media/media_clip_reader.h"

namespace Media {
namespace Clip {

Reader *const ReaderPointer::BadPointer = SharedMemoryLocation<Reader, 0>();

ReaderPointer::~ReaderPointer() {
	if (valid()) {
		delete _pointer;
	}
	_pointer = nullptr;
}

} // namespace Clip
} // namespace Media

namespace {

AnimationManager *_manager = nullptr;
bool AnimationsDisabled = false;

} // namespace

namespace anim {

transition linear = [](const float64 &delta, const float64 &dt) {
	return delta * dt;
};

transition sineInOut = [](const float64 &delta, const float64 &dt) {
	return -(delta / 2) * (cos(M_PI * dt) - 1);
};

transition halfSine = [](const float64 &delta, const float64 &dt) {
	return delta * sin(M_PI * dt / 2);
};

transition easeOutBack = [](const float64 &delta, const float64 &dt) {
	static constexpr auto s = 1.70158;

	const float64 t = dt - 1;
	return delta * (t * t * ((s + 1) * t + s) + 1);
};

transition easeInCirc = [](const float64 &delta, const float64 &dt) {
	return -delta * (sqrt(1 - dt * dt) - 1);
};

transition easeOutCirc = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * sqrt(1 - t * t);
};

transition easeInCubic = [](const float64 &delta, const float64 &dt) {
	return delta * dt * dt * dt;
};

transition easeOutCubic = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * (t * t * t + 1);
};

transition easeInQuint = [](const float64 &delta, const float64 &dt) {
	const float64 t2 = dt * dt;
	return delta * t2 * t2 * dt;
};

transition easeOutQuint = [](const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1, t2 = t * t;
	return delta * (t2 * t2 * t + 1);
};

void startManager() {
	stopManager();

	_manager = new AnimationManager();
}

void stopManager() {
	delete _manager;
	_manager = nullptr;

	Media::Clip::Finish();
}

void registerClipManager(not_null<Media::Clip::Manager*> manager) {
	Expects(_manager != nullptr);

	_manager->registerClip(manager);
}

bool Disabled() {
	return AnimationsDisabled;
}

void SetDisabled(bool disabled) {
	AnimationsDisabled = disabled;
	if (disabled && _manager) {
		_manager->step();
	}
}

void DrawStaticLoading(
		QPainter &p,
		QRectF rect,
		int stroke,
		QPen pen,
		QBrush brush) {
	PainterHighQualityEnabler hq(p);

	p.setBrush(brush);
	pen.setWidthF(stroke);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.drawEllipse(rect);

	const auto center = rect.center();
	const auto first = QPointF(center.x(), rect.y() + 1.5 * stroke);
	const auto delta = center.y() - first.y();
	const auto second = QPointF(center.x() + delta * 2 / 3., center.y());
	if (delta > 0) {
		QPainterPath path;
		path.moveTo(first);
		path.lineTo(center);
		path.lineTo(second);
		p.drawPath(path);
	}
}

} // anim

void BasicAnimation::start() {
	if (!_manager) return;

	_callbacks.start();
	_manager->start(this);
	_animating = true;
}

void BasicAnimation::stop() {
	if (!_manager) return;

	_animating = false;
	_manager->stop(this);
}

AnimationManager::AnimationManager() : _timer(this) {
	_timer.setSingleShot(false);
	connect(&_timer, &QTimer::timeout, this, &AnimationManager::step);
}

void AnimationManager::start(BasicAnimation *obj) {
	if (_iterating) {
		_starting.insert(obj);
		if (!_stopping.empty()) {
			_stopping.erase(obj);
		}
	} else {
		if (_objects.empty()) {
			_timer.start(AnimationTimerDelta);
		}
		_objects.insert(obj);
	}
}

void AnimationManager::stop(BasicAnimation *obj) {
	if (_iterating) {
		_stopping.insert(obj);
		if (!_starting.empty()) {
			_starting.erase(obj);
		}
	} else {
		auto i = _objects.find(obj);
		if (i != _objects.cend()) {
			_objects.erase(i);
			if (_objects.empty()) {
				_timer.stop();
			}
		}
	}
}

void AnimationManager::registerClip(not_null<Media::Clip::Manager*> clip) {
	connect(
		clip,
		&Media::Clip::Manager::callback,
		this,
		&AnimationManager::clipCallback);
}

void AnimationManager::step() {
	_iterating = true;
	const auto ms = getms();
	for (const auto object : _objects) {
		if (!_stopping.contains(object)) {
			object->step(ms, true);
		}
	}
	_iterating = false;

	if (!_starting.empty()) {
		for (const auto object : _starting) {
			_objects.emplace(object);
		}
		_starting.clear();
	}
	if (!_stopping.empty()) {
		for (const auto object : _stopping) {
			_objects.erase(object);
		}
		_stopping.clear();
	}
	if (_objects.empty()) {
		_timer.stop();
	}
}

void AnimationManager::clipCallback(
		Media::Clip::Reader *reader,
		qint32 threadIndex,
		qint32 notification) {
	Media::Clip::Reader::callback(
		reader,
		threadIndex,
		Media::Clip::Notification(notification));
}

