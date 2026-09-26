/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class QCursor;

namespace Ui {

class MiddleClickAutoscroll final {
public:
	explicit MiddleClickAutoscroll(
		Fn<void(int)> scrollBy,
		Fn<void(const QCursor &)> applyCursor = nullptr,
		Fn<void()> restoreCursor = nullptr,
		Fn<bool()> shouldContinue = nullptr);

	[[nodiscard]] bool active() const {
		return _active;
	}

	void toggleOrBeginHold(const QPoint &globalPosition);
	[[nodiscard]] bool finishHold(Qt::MouseButton button);
	void start(const QPoint &globalPosition);
	void stop();

private:
	enum class CursorMode {
		Neutral,
		Up,
		Down,
	};

	void updateCursor(int delta);
	void onTimer();
	[[nodiscard]] static QCursor makeCursor(CursorMode mode);

	Fn<void(int)> _scrollBy;
	Fn<void(const QCursor &)> _applyCursor;
	Fn<void()> _restoreCursor;
	Fn<bool()> _shouldContinue;
	bool _active = false;
	bool _middlePressed = false;
	QPoint _startPosition;
	crl::time _middlePressedAt = 0;
	crl::time _time = 0;
	base::Timer _timer;

};

} // namespace Ui
