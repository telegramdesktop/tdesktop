/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

#include <QtCore/QPointer>
#include <QtCore/QRectF>

class QWidget;

namespace Wallet {

class CardAngle final : public base::has_weak_ptr {
public:
	explicit CardAngle(Fn<void()> repaint);

	void setRepaint(Fn<void()> repaint);

	// Degrees clockwise, following the cursor in the card's active window.
	void track(not_null<QWidget*> widget, QRectF card);
	void stopTracking();

	[[nodiscard]] float64 value(crl::time now) const;

private:
	[[nodiscard]] float64 progress(crl::time now) const;
	void follow(QPointF cursor, bool repaint);
	void turnTo(float64 target, bool repaint);
	void cursorEvent(QPointF cursor);
	void windowActivated();
	[[nodiscard]] std::optional<QPointF> cursorInWindow() const;
	void scheduleStop();
	[[nodiscard]] bool onScreen() const;
	[[nodiscard]] bool windowActive() const;
	void subscribe(not_null<QWidget*> widget);

	Fn<void()> _repaint;
	QPointer<QWidget> _widget;
	QRectF _card;
	std::optional<QPointF> _cursor;
	Ui::Animations::Basic _animation;
	float64 _from = 0.;
	float64 _to = 0.;
	rpl::lifetime _tracking;
	bool _stopScheduled = false;
	bool _shown = false;

};

} // namespace Wallet
