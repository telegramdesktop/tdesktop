/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class ContinuousSlider : public TWidget {
public:
	ContinuousSlider(QWidget *parent);

	enum class Direction {
		Horizontal,
		Vertical,
	};
	void setDirection(Direction direction) {
		_direction = direction;
		update();
	}

	float64 value() const;
	void setValue(float64 value);
	void setFadeOpacity(float64 opacity);
	void setDisabled(bool disabled);
	bool isDisabled() const {
		return _disabled;
	}

	using Callback = Fn<void(float64)>;
	void setChangeProgressCallback(Callback &&callback) {
		_changeProgressCallback = std::move(callback);
	}
	void setChangeFinishedCallback(Callback &&callback) {
		_changeFinishedCallback = std::move(callback);
	}
	bool isChanging() const {
		return _mouseDown;
	}

	void setMoveByWheel(bool move);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	float64 fadeOpacity() const {
		return _fadeOpacity;
	}
	float64 getCurrentValue() {
		return _mouseDown ? _downValue : _value;
	}
	float64 getCurrentOverFactor(TimeMs ms) {
		return _disabled ? 0. : _a_over.current(ms, _over ? 1. : 0.);
	}
	Direction getDirection() const {
		return _direction;
	}
	bool isHorizontal() const {
		return (_direction == Direction::Horizontal);
	}

private:
	virtual QRect getSeekRect() const = 0;
	virtual float64 getOverDuration() const = 0;

	bool moveByWheel() const {
		return _byWheelFinished != nullptr;
	}

	void setOver(bool over);
	float64 computeValue(const QPoint &pos) const;
	void updateDownValueFromPos(const QPoint &pos);

	Direction _direction = Direction::Horizontal;
	bool _disabled = false;

	std::unique_ptr<SingleTimer> _byWheelFinished;

	Callback _changeProgressCallback;
	Callback _changeFinishedCallback;

	bool _over = false;
	Animation _a_over;

	float64 _value = 0.;

	bool _mouseDown = false;
	float64 _downValue = 0.;

	float64 _fadeOpacity = 1.;

};

class FilledSlider : public ContinuousSlider {
public:
	FilledSlider(QWidget *parent, const style::FilledSlider &st);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QRect getSeekRect() const override;
	float64 getOverDuration() const override;

	const style::FilledSlider &_st;

};

class MediaSlider : public ContinuousSlider {
public:
	MediaSlider(QWidget *parent, const style::MediaSlider &st);

	void setAlwaysDisplayMarker(bool alwaysDisplayMarker) {
		_alwaysDisplayMarker = alwaysDisplayMarker;
		update();
	}
	void disablePaint(bool disabled);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QRect getSeekRect() const override;
	float64 getOverDuration() const override;

	const style::MediaSlider &_st;
	bool _alwaysDisplayMarker = false;
	bool _paintDisabled = false;

};

} // namespace Ui
