/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QWheelEvent;

namespace Ui {

class ElasticScroll;
class ScrollArea;

// `before` lets a scroll keep a wheel handler of its own - one that
// claims some events and leaves the plain ones alone, like a modifier
// shortcut - and still scroll smoothly for everything it declines.
void InstallSmoothScroll(
	not_null<ScrollArea*> scroll,
	Fn<bool(not_null<QWheelEvent*>)> before = nullptr);
void InstallSmoothScroll(
	not_null<ElasticScroll*> scroll,
	Fn<bool(not_null<QWheelEvent*>)> before = nullptr);

// Called for every wheel event target, so that the scroll it belongs to
// gets its handler on first use instead of at every creation site. The
// call sits before the event is dispatched, so even the first notch a
// scroll ever receives is already animated.
void EnsureSmoothScroll(not_null<QObject*> wheelTarget);

// A scroll that drives the wheel itself through setCustomWheelProcess
// must call this while it is being set up: the attach above happens on
// the first wheel event, which is always later, and would replace the
// handler that was set there.
void DisableSmoothScroll(not_null<QWidget*> scroll);

} // namespace Ui
