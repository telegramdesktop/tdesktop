/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"
#include "ui/rect_part.h"

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
	void paint(QPainter &p, bool over, bool active);

private:
	const style::MediaSpeedButton &_st;

	float64 _speed = 1.;

	QFontMetricsF _metrics;
	float64 _adjustedAscent = 0.;
	float64 _adjustedHeight = 0.;

	QString _text;
	float64 _textWidth = 0;

	Fn<void()> _callback;

};

class SpeedButton final : public Ui::RippleButton {
public:
	SpeedButton(QWidget *parent, const style::MediaSpeedButton &st);

	[[nodiscard]] const style::MediaSpeedButton &st() const {
		return _st;
	}

	void setSpeed(float64 speed);

private:
	void paintEvent(QPaintEvent *e) override;

	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	const style::MediaSpeedButton &_st;
	SpeedButtonLayout _layout;
	bool _isDefault = false;

};

class SettingsButton final : public Ui::RippleButton {
public:
	SettingsButton(QWidget *parent, const style::MediaSpeedButton &st);

	[[nodiscard]] const style::MediaSpeedButton &st() const {
		return _st;
	}

	void setSpeed(float64 speed);
	void setQuality(int quality);
	void setActive(bool active);

private:
	void paintEvent(QPaintEvent *e) override;

	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	void onStateChanged(State was, StateChangeSource source) override;

	void paintBadge(
		QPainter &p,
		const QString &text,
		RectPart origin,
		QColor color);
	void prepareFrame();

	const style::MediaSpeedButton &_st;
	Ui::Animations::Simple _activeAnimation;
	Ui::Animations::Simple _overAnimation;
	QImage _frameCache;
	float _speed = 1.;
	int _quality = 0;
	bool _isDefaultSpeed = false;
	bool _active = false;

};

} // namespace Media::Player
