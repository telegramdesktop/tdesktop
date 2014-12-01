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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "animation.h"
#include <QtCore/QTimer>

namespace {
	AnimationManager *manager = 0;
};

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

	void start(Animated *obj) {
		if (!manager) return;
		manager->start(obj);
	}

	void stop(Animated *obj) {
		if (!manager) return;
		manager->stop(obj);
	}

	void startManager() {
		delete manager;
		manager = new AnimationManager();
	}

	void stopManager() {
		delete manager;
		manager = 0;
	}

}
