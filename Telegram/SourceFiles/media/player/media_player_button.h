/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

#include <QtGui/QFontMetrics>

namespace style {
struct MediaPlayerButton;
struct MediaSpeedButton;
} // namespace style

namespace Media::Player {

class PlayButtonLayout {
public:
	enum class State {
		Play,
		Pause,
		Cancel,
	};
	PlayButtonLayout(const style::MediaPlayerButton &st, Fn<void()> callback);

	void setState(State state);
	void finishTransform();
	void paint(QPainter &p, const QBrush &brush);

private:
	void animationCallback();
	void startTransform(float64 from, float64 to);

	void paintPlay(QPainter &p, const QBrush &brush);
	void paintPlayToPause(QPainter &p, const QBrush &brush, float64 progress);
	void paintPlayToCancel(QPainter &p, const QBrush &brush, float64 progress);
	void paintPauseToCancel(QPainter &p, const QBrush &brush, float64 progress);

	const style::MediaPlayerButton &_st;

	State _state = State::Play;
	State _oldState = State::Play;
	State _nextState = State::Play;
	Ui::Animations::Simple _transformProgress;
	bool _transformBackward = false;

	Fn<void()> _callback;

};

class SpeedButtonLayout {
public:
	SpeedButtonLayout(
		const style::MediaSpeedButton &st,
		Fn<void()> callback,
		float64 speed);

	void setSpeed(float64 speed);
	void finishTransform();
	void paint(QPainter &p, const QColor &color);

private:
	void animationCallback();
	void startTransform(float64 from, float64 to);

	const style::MediaSpeedButton &_st;

	float64 _speed = 1.;
	float64 _oldSpeed = 1.;
	float64 _nextSpeed = 1.;
	std::optional<QColor> _lastPaintColor;
	std::optional<QColor> _oldColor;
	Ui::Animations::Simple _transformProgress;
	bool _transformBackward = false;

	QFontMetricsF _metrics;

	QString _text;
	float64 _textWidth = 0;
	QString _oldText;
	float64 _oldTextWidth = 0;

	Fn<void()> _callback;

};

} // namespace Media::Player
