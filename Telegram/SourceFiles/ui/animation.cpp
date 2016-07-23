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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "animation.h"

#include "media/media_clip_reader.h"

namespace {

AnimationManager *_manager = nullptr;

} // namespace

namespace anim {

float64 linear(const float64 &delta, const float64 &dt) {
	return delta * dt;
}

float64 sineInOut(const float64 &delta, const float64 &dt) {
	return -(delta / 2) * (cos(M_PI * dt) - 1);
}

float64 halfSine(const float64 &delta, const float64 &dt) {
	return delta * sin(M_PI * dt / 2);
}

float64 easeOutBack(const float64 &delta, const float64 &dt) {
	static const float64 s = 1.70158;

	const float64 t = dt - 1;
	return delta * (t * t * ((s + 1) * t + s) + 1);
}

float64 easeInCirc(const float64 &delta, const float64 &dt) {
	return -delta * (sqrt(1 - dt * dt) - 1);
}

float64 easeOutCirc(const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * sqrt(1 - t * t);
}

float64 easeInCubic(const float64 &delta, const float64 &dt) {
	return delta * dt * dt * dt;
}

float64 easeOutCubic(const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1;
	return delta * (t * t * t + 1);
}

float64 easeInQuint(const float64 &delta, const float64 &dt) {
	const float64 t2 = dt * dt;
	return delta * t2 * t2 * dt;
}

float64 easeOutQuint(const float64 &delta, const float64 &dt) {
	const float64 t = dt - 1, t2 = t * t;
	return delta * (t2 * t2 * t + 1);
}

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

} // anim

void Animation::start() {
	if (!_manager) return;

	_callbacks.start();
	_manager->start(this);
	_animating = true;
}

void Animation::stop() {
	if (!_manager) return;

	_animating = false;
	_manager->stop(this);
}

AnimationManager::AnimationManager() : _timer(this), _iterating(false) {
	_timer.setSingleShot(false);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(timeout()));
}

void AnimationManager::start(Animation *obj) {
	if (_iterating) {
		_starting.insert(obj, NullType());
		if (!_stopping.isEmpty()) {
			_stopping.remove(obj);
		}
	} else {
		if (_objects.isEmpty()) {
			_timer.start(AnimationTimerDelta);
		}
		_objects.insert(obj, NullType());
	}
}

void AnimationManager::stop(Animation *obj) {
	if (_iterating) {
		_stopping.insert(obj, NullType());
		if (!_starting.isEmpty()) {
			_starting.remove(obj);
		}
	} else {
		AnimatingObjects::iterator i = _objects.find(obj);
		if (i != _objects.cend()) {
			_objects.erase(i);
			if (_objects.isEmpty()) {
				_timer.stop();
			}
		}
	}
}

void AnimationManager::timeout() {
	_iterating = true;
	uint64 ms = getms();
	for (AnimatingObjects::const_iterator i = _objects.begin(), e = _objects.end(); i != e; ++i) {
		if (!_stopping.contains(i.key())) {
			i.key()->step(ms, true);
		}
	}
	_iterating = false;

	if (!_starting.isEmpty()) {
		for (AnimatingObjects::iterator i = _starting.begin(), e = _starting.end(); i != e; ++i) {
			_objects.insert(i.key(), NullType());
		}
		_starting.clear();
	}
	if (!_stopping.isEmpty()) {
		for (AnimatingObjects::iterator i = _stopping.begin(), e = _stopping.end(); i != e; ++i) {
			_objects.remove(i.key());
		}
		_stopping.clear();
	}
	if (!_objects.size()) {
		_timer.stop();
	}
}

void AnimationManager::clipCallback(Media::Clip::Reader *reader, qint32 threadIndex, qint32 notification) {
	Media::Clip::Reader::callback(reader, threadIndex, Media::Clip::Notification(notification));
}

