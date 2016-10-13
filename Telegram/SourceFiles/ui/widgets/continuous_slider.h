/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

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
	void setValue(float64 value, bool animated);
	void setFadeOpacity(float64 opacity);
	void setDisabled(bool disabled);

	using Callback = base::lambda_unique<void(float64)>;
	void setChangeProgressCallback(Callback &&callback) {
		_changeProgressCallback = std_::move(callback);
	}
	void setChangeFinishedCallback(Callback &&callback) {
		_changeFinishedCallback = std_::move(callback);
	}
	bool isChanging() const {
		return _mouseDown;
	}

	void setMoveByWheel(bool moveByWheel);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

	float64 fadeOpacity() const {
		return _fadeOpacity;
	}
	float64 getCurrentValue(uint64 ms) {
		_a_value.step(ms);
		return _mouseDown ? _downValue : a_value.current();
	}
	float64 getCurrentOverFactor(uint64 ms) {
		return _disabled ? 0. : _a_over.current(ms, _over ? 1. : 0.);
	}
	bool isDisabled() const {
		return _disabled;
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

	void step_value(float64 ms, bool timer);
	void setOver(bool over);
	float64 computeValue(const QPoint &pos) const;
	void updateDownValueFromPos(const QPoint &pos);

	Direction _direction = Direction::Horizontal;
	bool _disabled = false;

	bool _moveByWheel = false;
	std_::unique_ptr<SingleTimer> _byWheelFinished;

	Callback _changeProgressCallback;
	Callback _changeFinishedCallback;

	bool _over = false;
	FloatAnimation _a_over;

	anim::fvalue a_value = { 0., 0. };
	Animation _a_value;

	bool _mouseDown = false;
	float64 _downValue = 0.;

	float64 _fadeOpacity = 1.;

};

} // namespace Ui
