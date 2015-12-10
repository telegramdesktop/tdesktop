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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "animation.h"

#include "mainwidget.h"
#include "window.h"

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

	void step(Animated *obj) {
		if (!manager) return;
		manager->step(obj);
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

bool AnimatedGif::animStep(float64 ms) {
	int32 f = frame;
	while (f < images.size() && ms > delays[f]) {
		++f;
		if (f == images.size() && images.size() < framesCount) {
			if (reader->read(&img)) {
				int64 d = reader->nextImageDelay(), delay = delays[f - 1];
				if (!d) d = 1;
				delay += d;
				if (img.size() != QSize(w, h)) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
				images.push_back(img);
				frames.push_back(QPixmap());
				delays.push_back(delay);
				for (int32 i = 0; i < images.size(); ++i) {
					if (!images[i].isNull() || !frames[i].isNull()) {
						images[i] = QImage();
						frames[i] = QPixmap();
						break;
					}
				}
			} else {
				framesCount = images.size();
			}
		}
		if (f == images.size()) {
			if (!duration) {
				duration = delays.isEmpty() ? 1 : delays.back();
			}

			f = 0;
			for (int32 i = 0, s = delays.size() - 1; i <= s; ++i) {
				delays[i] += duration;
			}
			if (images[f].isNull()) {
				QString fname = reader->fileName();
				delete reader;
				reader = new QImageReader(fname);
			}
		}
		if (images[f].isNull() && reader->read(&img)) {
			if (img.size() != QSize(w, h)) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			images[f] = img;
			frames[f] = QPixmap();
		}
	}
	if (frame != f) {
		frame = f;
		if (msg && App::main()) {
			App::main()->msgUpdated(msg);
		} else {
			emit updated();
		}
	}
	return true;
}

void AnimatedGif::start(HistoryItem *row, const FileLocation &f) {
	stop();

	file = new FileLocation(f);
	if (!file->accessEnable()) {
		stop();
		return;
	}
	access = true;

	reader = new QImageReader(file->name());
	if (!reader->canRead() || !reader->supportsAnimation()) {
		stop();
		return;
	}

	QSize s = reader->size();
	w = s.width();
	h = s.height();
	framesCount = reader->imageCount();
	if (!w || !h || !framesCount) {
		stop();
		return;
	}

	frames.reserve(framesCount);
	images.reserve(framesCount);
	delays.reserve(framesCount);

	int32 sizeLeft = MediaViewImageSizeLimit, delay = 0;
	for (bool read = reader->read(&img); read; read = reader->read(&img)) {
		sizeLeft -= w * h * 4;
		if (img.size() != s) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		images.push_back(img);
		frames.push_back(QPixmap());
		int32 d = reader->nextImageDelay();
		if (!d) d = 1;
		delay += d;
		delays.push_back(delay);
		if (sizeLeft < 0) break;
	}

	msg = row;

	anim::start(this);
	if (msg) {
		msg->initDimensions();
		if (App::main()) App::main()->itemResized(msg, true);
	}
}

void AnimatedGif::stop(bool onItemRemoved) {
	if (file) {
		if (access) {
			file->accessDisable();
		}
		delete file;
		file = 0;
	}
	access = false;

	if (isNull()) return;

	delete reader;
	reader = 0;
	HistoryItem *row = msg;
	msg = 0;
	frames.clear();
	images.clear();
	delays.clear();
	w = h = frame = framesCount = duration = 0;

	anim::stop(this);
	if (row && !onItemRemoved) {
		row->initDimensions();
		if (App::main()) App::main()->itemResized(row, true);
	}
}

const QPixmap &AnimatedGif::current(int32 width, int32 height, bool rounded) {
	if (!width) width = w;
	if (!height) height = h;
	if ((frames[frame].isNull() || frames[frame].width() != width || frames[frame].height() != height) && !images[frame].isNull()) {
		QImage img = images[frame];
		if (img.width() != width || img.height() != height) img = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (rounded) imageRound(img);
		frames[frame] = QPixmap::fromImage(img, Qt::ColorOnly);
		frames[frame].setDevicePixelRatio(cRetinaFactor());
	}
	return frames[frame];
}
