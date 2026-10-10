/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_gradient.h"

#include <QtCore/QPointer>
#include <QtCore/QRectF>

class QWidget;

namespace Wallet {

class CardAngle final : public base::has_weak_ptr {
public:
	CardAngle();

	// Degrees clockwise, following the cursor in the card's active window.
	void track(
		not_null<const void*> card,
		not_null<QWidget*> widget,
		QRectF rect);
	void forget(not_null<const void*> card);

	[[nodiscard]] float64 value(crl::time now) const;
	[[nodiscard]] CardBackground &background();

private:
	struct Card {
		const void *key = nullptr;
		QPointer<QWidget> widget;
		QRect rect;
		float64 center = 0.;
	};

	[[nodiscard]] float64 progress(crl::time now) const;
	void follow(bool repaint, bool immediate = false);
	void turnTo(float64 target, bool repaint, bool immediate);
	void repaintCards();
	[[nodiscard]] bool pruneCards();
	void cursorEvent(QPointF cursor);
	void windowActivated();
	[[nodiscard]] std::optional<float64> cursorInWindow() const;
	void scheduleFollow();
	void scheduleStop();
	void stopTracking();
	[[nodiscard]] bool windowActive() const;
	void subscribe(not_null<QWidget*> window);

	std::vector<Card> _cards;
	QPointer<QWidget> _window;
	std::optional<float64> _cursor;
	Ui::Animations::Basic _animation;
	CardBackground _background;
	float64 _from = 0.;
	float64 _to = 0.;
	float64 _ramp = 0.;
	float64 _center = 0.;
	rpl::lifetime _tracking;
	bool _followScheduled = false;
	bool _stopScheduled = false;
	bool _shown = false;

};

} // namespace Wallet
