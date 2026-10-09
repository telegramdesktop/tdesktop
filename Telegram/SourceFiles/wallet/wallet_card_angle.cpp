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

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kFollowJump = 6.;
constexpr auto kFollowDuration = crl::time(340);
constexpr auto kFollowRamp = 0.2;
constexpr auto kAngleLimit = 60.;
constexpr auto kAngleStep = 0.25; // one colour level of the card background
constexpr auto kCenterSlack = 1.;

[[nodiscard]] float64 FollowEase(float64 progress, float64 ramp) {
	if (progress > 0.5) {
		return 1. - FollowEase(1. - progress, ramp);
	}
	const auto speed = 1. / (1. - ramp);
	return (progress < ramp)
		? (speed * progress * progress / (2. * ramp))
		: (speed * (progress - ramp / 2.));
}

// 60 * sign(dx) * dx^2 / (dx^2 + D^2): flat at 0, 30 at D, never 60.
[[nodiscard]] float64 CursorAngle(float64 dx) {
	const auto reach = float64(st::walletCardAngleReach);
	const auto squared = dx * dx;
	return ((dx < 0.) ? -kAngleLimit : kAngleLimit)
		* squared
		/ (squared + reach * reach);
}

} // namespace

CardAngle::CardAngle() {
	_animation.init([=](crl::time now) {
		repaintCards();
		return now < _animation.started() + kFollowDuration;
	});
}

void CardAngle::track(
		not_null<const void*> card,
		not_null<QWidget*> widget,
		QRectF rect) {
	_stopScheduled = false;
	const auto window = widget->window();
	const auto aligned = rect.toAlignedRect();
	const auto center = rect.center().x()
		+ widget->mapTo(window, QPoint()).x();
	if (_window != window) {
		if (!widget->isVisible()) {
			_shown = true;
			return;
		} else if (!widget->visibleRegion().intersects(aligned)) {
			return;
		}
		stopTracking();
		_window = window;
		subscribe(window);
		_cards.push_back({ card.get(), widget.get(), aligned, center });
		_center = center;
		_cursor = windowActive() ? cursorInWindow() : std::nullopt;
		follow(false, !std::exchange(_shown, true));
		return;
	}
	const auto i = ranges::find(_cards, card.get(), &Card::key);
	if (i != end(_cards)) {
		i->widget = widget.get();
		i->rect = aligned;
		i->center = center;
	} else {
		_cards.push_back({ card.get(), widget.get(), aligned, center });
	}
	if (std::abs(center - _center) >= kCenterSlack) {
		scheduleFollow();
	}
}

void CardAngle::forget(not_null<const void*> card) {
	const auto i = ranges::find(_cards, card.get(), &Card::key);
	if (i == end(_cards)) {
		return;
	}
	_cards.erase(i);
	if (_cards.empty()) {
		scheduleStop();
	}
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
	return _from + (_to - _from) * FollowEase(progress(now), _ramp);
}

CardBackground &CardAngle::background() {
	return _background;
}

void CardAngle::follow(bool repaint, bool immediate) {
	if (_cards.empty()) {
		return;
	}
	auto centers = std::vector<float64>();
	centers.reserve(_cards.size());
	for (const auto &card : _cards) {
		centers.push_back(card.center);
	}
	const auto middle = begin(centers) + centers.size() / 2;
	std::nth_element(begin(centers), middle, end(centers));
	_center = *middle;
	if (!_cursor || !windowActive()) {
		return;
	}
	const auto law = CursorAngle(*_cursor - _center);
	turnTo(std::trunc(law / kAngleStep) * kAngleStep, repaint, immediate);
}

void CardAngle::turnTo(float64 target, bool repaint, bool immediate) {
	const auto now = crl::now();
	if (_animation.animating() && target == _to) {
		return;
	}
	const auto shown = value(now);
	if (target == shown) {
		return;
	}
	if (immediate
		|| anim::Disabled()
		|| std::abs(target - shown) <= kFollowJump) {
		_animation.stop();
		_from = _to = target;
	} else if (_animation.animating()
		&& (std::abs(target - _to) * FollowEase(progress(now), _ramp)
			<= kFollowJump)) {
		// WHY: restarting the ease-in on every move of a fast cursor would
		// keep the sweep crawling, so a target whose on-screen step stays
		// within the jump retargets the running turn in place.
		_to = target;
	} else {
		_from = shown;
		_to = target;
		// Peak speed never exceeds the full range over the duration.
		_ramp = std::clamp(
			1. - std::abs(_to - _from) / (2. * kAngleLimit),
			0.,
			kFollowRamp);
		_animation.start();
	}
	if (repaint) {
		repaintCards();
	}
}

void CardAngle::repaintCards() {
	for (const auto &card : _cards) {
		if (const auto widget = card.widget.data()) {
			widget->update(card.rect);
		}
	}
}

bool CardAngle::pruneCards() {
	auto widget = static_cast<QWidget*>(nullptr);
	auto region = QRegion();
	const auto hidden = [&](const Card &card) {
		const auto strong = card.widget.data();
		if (!strong || !strong->isVisible()) {
			return true;
		} else if (strong != widget) {
			widget = strong;
			region = strong->visibleRegion();
		}
		return !region.intersects(card.rect);
	};
	_cards.erase(
		std::remove_if(begin(_cards), end(_cards), hidden),
		end(_cards));
	return !_cards.empty();
}

void CardAngle::cursorEvent(QPointF cursor) {
	if (!pruneCards()) {
		scheduleStop();
		return;
	} else if (!windowActive()
		|| !QRectF(QPointF(), _window->size()).contains(cursor)) {
		_cursor = std::nullopt;
		return;
	}
	_cursor = cursor.x();
	follow(true);
}

void CardAngle::windowActivated() {
	if (!pruneCards()) {
		scheduleStop();
		return;
	}
	if (const auto x = cursorInWindow()) {
		_cursor = x;
		follow(true);
	}
}

std::optional<float64> CardAngle::cursorInWindow() const {
	if (!_window) {
		return std::nullopt;
	}
	const auto local = QPointF(_window->mapFromGlobal(QCursor::pos()));
	return QRectF(QPointF(), _window->size()).contains(local)
		? std::make_optional(local.x())
		: std::nullopt;
}

void CardAngle::scheduleFollow() {
	if (!std::exchange(_followScheduled, true)) {
		crl::on_main(this, [=] {
			_followScheduled = false;
			follow(true);
		});
	}
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

void CardAngle::stopTracking() {
	_tracking.destroy();
	_cards.clear();
	_background.clear();
	_window = nullptr;
	_cursor = std::nullopt;
	_stopScheduled = false;
	if (_animation.animating()) {
		_from = _to = value(crl::now());
		_animation.stop();
	}
}

bool CardAngle::windowActive() const {
	return _window && _window->isActiveWindow();
}

void CardAngle::subscribe(not_null<QWidget*> window) {
	const auto handle = window->windowHandle();
	if (!handle) {
		return;
	}
	base::install_event_filter(handle, [=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::MouseMove:
		case QEvent::NonClientAreaMouseMove:
			cursorEvent(static_cast<QMouseEvent*>(e.get())->windowPos());
			break;
		case QEvent::Enter:
			cursorEvent(static_cast<QEnterEvent*>(e.get())->windowPos());
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
