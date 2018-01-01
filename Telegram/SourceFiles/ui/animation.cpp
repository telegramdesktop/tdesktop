/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

void registerClipManager(Media::Clip::Manager *manager) {
	manager->connect(manager, SIGNAL(callback(Media::Clip::Reader*,qint32,qint32)), _manager, SLOT(clipCallback(Media::Clip::Reader*,qint32,qint32)));
}

bool Disabled() {
	return AnimationsDisabled;
}

void SetDisabled(bool disabled) {
	AnimationsDisabled = disabled;
	if (disabled && _manager) {
		_manager->timeout();
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

AnimationManager::AnimationManager() : _timer(this), _iterating(false) {
	_timer.setSingleShot(false);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(timeout()));
}

void AnimationManager::start(BasicAnimation *obj) {
	if (_iterating) {
		_starting.insert(obj);
		if (!_stopping.isEmpty()) {
			_stopping.remove(obj);
		}
	} else {
		if (_objects.isEmpty()) {
			_timer.start(AnimationTimerDelta);
		}
		_objects.insert(obj);
	}
}

void AnimationManager::stop(BasicAnimation *obj) {
	if (_iterating) {
		_stopping.insert(obj);
		if (!_starting.isEmpty()) {
			_starting.remove(obj);
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

void AnimationManager::timeout() {
	_iterating = true;
	auto ms = getms();
	for_const (auto object, _objects) {
		if (!_stopping.contains(object)) {
			object->step(ms, true);
		}
	}
	_iterating = false;

	if (!_starting.isEmpty()) {
		for_const (auto object, _starting) {
			_objects.insert(object);
		}
		_starting.clear();
	}
	if (!_stopping.isEmpty()) {
		for_const (auto object, _stopping) {
			_objects.remove(object);
		}
		_stopping.clear();
	}
	if (_objects.empty()) {
		_timer.stop();
	}
}

void AnimationManager::clipCallback(Media::Clip::Reader *reader, qint32 threadIndex, qint32 notification) {
	Media::Clip::Reader::callback(reader, threadIndex, Media::Clip::Notification(notification));
}

