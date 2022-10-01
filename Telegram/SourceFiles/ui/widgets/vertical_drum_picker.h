/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "ui/effects/animations.h"

namespace Ui {

class PickerAnimation final {
public:
	using Shift = float64;
	PickerAnimation();

	void jumpToOffset(int offset);
	void setResult(float64 from, float64 current, float64 to);

	[[nodiscard]] rpl::producer<Shift> updates() const;

private:
	Ui::Animations::Simple _animation;
	struct {
		float64 from = 0.;
		float64 current = 0.;
		float64 to = 0.;
	} _result;

	rpl::event_stream<Shift> _updates;
};

class VerticalDrumPicker final : public Ui::RpWidget {
public:
	using PaintItemCallback = Fn<void(
		QPainter &p,
		int index,
		float y,
		float64 distanceFromCenter,
		int outerWidth)>;

	VerticalDrumPicker(
		not_null<Ui::RpWidget*> parent,
		PaintItemCallback &&paintCallback,
		int itemsCount,
		int itemHeight,
		int startIndex = 0,
		bool looped = false);

	[[nodiscard]] int index() const;

	void handleWheelEvent(not_null<QWheelEvent*> e);
	void handleMouseEvent(not_null<QMouseEvent*> e);
	void handleKeyEvent(not_null<QKeyEvent*> e);

protected:
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void increaseShift(float64 by);
	void animationDataFromIndex();
	[[nodiscard]] int normalizedIndex(int index) const;
	[[nodiscard]] bool isIndexInRange(int index) const;

	const int _itemsCount;
	const int _itemHeight;

	PaintItemCallback _paintCallback;

	int _pendingStartIndex = 0;

	struct {
		int count = 0;
		int centerOffset = 0;
	} _itemsVisible;

	int _index = 0;
	float64 _shift = 0.;

	struct {
		const bool looped;
		int minIndex = 0;
		int maxIndex = 0;
	} _loopData;

	PickerAnimation _animation;

	struct {
		bool pressed = false;
		int lastPositionY;
		bool clickDisabled = false;
	} _mouse;

	struct {
		int verticalDelta = 0;
	} _touch;

};

} // namespace Ui
