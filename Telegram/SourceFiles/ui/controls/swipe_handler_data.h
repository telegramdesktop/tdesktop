/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui::Controls {

struct SwipeContextData final {
	[[nodiscard]] bool empty() const {
		return !ratio
			&& !reachRatio
			&& !translation
			&& !cursorTop;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	// The system reports the gesture deltas in the scroll direction, so with
	// the natural scrolling turned off they are the opposite of the finger
	// movement. The translation sign follows the gesture and tells which of
	// the two actions was started, while the content should follow the
	// finger, so these give the direction to paint it moving in.
	[[nodiscard]] int visualTranslation() const {
		return inverted ? translation : -translation;
	}
	[[nodiscard]] float64 visualExactTranslation() const {
		return inverted ? exactTranslation : -exactTranslation;
	}

	float64 ratio = 0.;
	float64 reachRatio = 0.;
	float64 exactTranslation = 0.;
	int64 msgBareId = 0;
	int translation = 0;
	int cursorTop = 0;
	bool inverted = false;
};

struct SwipeHandlerInitData final {
	// The direction follows the scroll, which is what a gesture that just
	// moves between two equal things should use. A gesture that drags out
	// something living at a fixed side of the window, like the main menu,
	// has to be started by moving the finger towards that side instead, so
	// it should look at this one and stay the same with any scroll setting.
	[[nodiscard]] Qt::LayoutDirection fingerDirection() const {
		return inverted
			? direction
			: ((direction == Qt::LeftToRight)
				? Qt::RightToLeft
				: Qt::LeftToRight);
	}

	QPoint cursorPosition;
	Qt::LayoutDirection direction = Qt::LeftToRight;
	bool inverted = false;
};

struct SwipeBackResult final {
	rpl::lifetime lifetime;
	Fn<void(SwipeContextData)> callback;
};

} // namespace Ui::Controls
