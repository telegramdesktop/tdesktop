/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_angle.h"

#include "base/event_filter.h"
#include "ui/effects/animation_value.h"

#include <QtGui/QCursor>
#include <QtGui/QWindow>

namespace Wallet {
namespace {

constexpr auto kFollowJump = 6.;
constexpr auto kFollowDuration = crl::time(330);
constexpr auto kFollowRamp = 0.2;
constexpr auto kSweepPeriod = 180.;

[[nodiscard]] float64 FollowEase(float64 progress) {
	if (progress > 0.5) {
		return 1. - FollowEase(1. - progress);
	}
	constexpr auto kSpeed = 1. / (1. - kFollowRamp);
	return (progress < kFollowRamp)
		? (kSpeed * progress * progress / (2. * kFollowRamp))
		: (kSpeed * (progress - kFollowRamp / 2.));
}

[[nodiscard]] std::optional<float64> CursorBearing(
		QPointF center,
		QPointF cursor) {
	const auto d = cursor - center;
	if (std::abs(d.x()) + std::abs(d.y()) < 0.5) {
		return std::nullopt;
	}
	return std::atan2(d.x(), -d.y()) * 180. / M_PI;
}

} // namespace

CardAngle::CardAngle(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
	_animation.init([=](crl::time now) {
		if (_repaint) {
			_repaint();
		}
		return now < _animation.started() + kFollowDuration;
	});
}

void CardAngle::setRepaint(Fn<void()> repaint) {
	_repaint = std::move(repaint);
}

void CardAngle::track(not_null<QWidget*> widget, QRectF card) {
	_stopScheduled = false;
	if (_widget != widget.get()) {
		if (!widget->isVisible()) {
			_shown = true;
			return;
		}
		if (!widget->visibleRegion().intersects(card.toAlignedRect())) {
			return;
		}
		stopTracking();
		_widget = widget.get();
		subscribe(widget);
		if (windowActive()) {
			_cursor = cursorInWindow();
		}
	}
	_card = card;
	if (_cursor && windowActive()) {
		follow(*_cursor, false);
	}
	_shown = true;
}

void CardAngle::stopTracking() {
	_tracking.destroy();
	_widget = nullptr;
	_cursor = std::nullopt;
	_stopScheduled = false;
}

float64 CardAngle::progress(crl::time now) const {
	return std::clamp(
		(now - _animation.started()) / float64(kFollowDuration),
		0.,
		1.);
}

float64 CardAngle::value(crl::time now) const {
	if (!_animation.animating()) {
		return _to;
	}
	return _from + (_to - _from) * FollowEase(progress(now));
}

void CardAngle::follow(QPointF cursor, bool repaint) {
	if (!_widget) {
		return;
	}
	const auto center = _widget->mapTo(_widget->window(), _card.center());
	if (const auto bearing = CursorBearing(center, cursor)) {
		turnTo(*bearing, repaint);
	}
}

void CardAngle::turnTo(float64 target, bool repaint) {
	const auto now = crl::now();
	if (_animation.animating()
		&& std::abs(std::remainder(target - _to, kSweepPeriod)) < 1e-6) {
		return;
	}
	const auto shown = value(now);
	const auto goal = shown + std::remainder(target - shown, kSweepPeriod);
	if (goal == shown) {
		return;
	}
	if (!_shown
		|| anim::Disabled()
		|| std::abs(goal - shown) <= kFollowJump) {
		_animation.stop();
		_from = _to = goal;
	} else if (_animation.animating()
		&& (std::abs(goal - _to) * FollowEase(progress(now))
			<= kFollowJump)) {
		// WHY: restarting the ease-in on every move of a fast cursor would
		// keep the sweep crawling, so a target whose on-screen step stays
		// within the jump retargets the running turn in place.
		_to = goal;
	} else {
		_from = shown;
		_to = goal;
		_animation.start();
	}
	if (repaint && _repaint) {
		_repaint();
	}
}

void CardAngle::cursorEvent(QPointF cursor) {
	if (!onScreen()) {
		scheduleStop();
		return;
	}
	const auto window = _widget->window();
	if (!windowActive()
		|| !QRectF(QPointF(), window->size()).contains(cursor)) {
		_cursor = std::nullopt;
		return;
	}
	_cursor = cursor;
	follow(cursor, true);
}

void CardAngle::windowActivated() {
	if (!onScreen()) {
		scheduleStop();
		return;
	}
	if (const auto local = cursorInWindow()) {
		_cursor = local;
		follow(*local, true);
	}
}

std::optional<QPointF> CardAngle::cursorInWindow() const {
	const auto window = _widget->window();
	const auto local = window->mapFromGlobal(QPointF(QCursor::pos()));
	return QRectF(QPointF(), window->size()).contains(local)
		? std::make_optional(local)
		: std::nullopt;
}

void CardAngle::scheduleStop() {
	if (!std::exchange(_stopScheduled, true)) {
		crl::on_main(this, [=] {
			if (_stopScheduled) {
				stopTracking();
			}
		});
	}
}

bool CardAngle::onScreen() const {
	return _widget
		&& _widget->isVisible()
		&& _widget->visibleRegion().intersects(_card.toAlignedRect());
}

bool CardAngle::windowActive() const {
	return _widget && _widget->window()->isActiveWindow();
}

void CardAngle::subscribe(not_null<QWidget*> widget) {
	const auto window = widget->window();
	const auto handle = window->windowHandle();
	if (!handle) {
		return;
	}
	base::install_event_filter(handle, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::MouseMove:
		case QEvent::NonClientAreaMouseMove:
			cursorEvent(static_cast<QMouseEvent*>(e.get())->position());
			break;
		case QEvent::Enter:
			cursorEvent(static_cast<QEnterEvent*>(e.get())->position());
			break;
		case QEvent::Leave:
			_cursor = std::nullopt;
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	}, _tracking);
	base::install_event_filter(window, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::WindowActivate) {
			windowActivated();
		} else if (e->type() == QEvent::WindowDeactivate) {
			_cursor = std::nullopt;
		}
		return base::EventFilterResult::Continue;
	}, _tracking);
}

} // namespace Wallet
