/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/single_timer.h"

#include "application.h"

SingleTimer::SingleTimer(QObject *parent) : QTimer(parent) {
	QTimer::setSingleShot(true);
	Sandbox::connect(SIGNAL(adjustSingleTimers()), this, SLOT(adjust()));
}

void SingleTimer::setTimeoutHandler(Fn<void()> handler) {
	if (_handler && !handler) {
		disconnect(this, SIGNAL(timeout()), this, SLOT(onTimeout()));
	} else if (handler && !_handler) {
		connect(this, SIGNAL(timeout()), this, SLOT(onTimeout()));
	}
	_handler = std::move(handler);
}

void SingleTimer::adjust() {
	auto n = getms(true);
	if (isActive()) {
		if (n >= _finishing) {
			start(0);
		} else {
			start(_finishing - n);
		}
	}
}

void SingleTimer::onTimeout() {
	if (_handler) {
		_handler();
	}
}

void SingleTimer::start(int msec) {
	_finishing = getms(true) + (msec < 0 ? 0 : msec);
	QTimer::start(msec);
}

void SingleTimer::startIfNotActive(int msec) {
	if (isActive()) {
		int remains = remainingTime();
		if (remains > msec) {
			start(msec);
		} else if (!remains) {
			start(1);
		}
	} else {
		start(msec);
	}
}
