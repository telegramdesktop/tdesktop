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

namespace style {
struct MediaSlider;
} // namespace style

namespace Ui {

class MediaSlider : public TWidget {
public:
	MediaSlider(QWidget *parent, const style::MediaSlider &st);

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

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	void step_value(float64 ms, bool timer);
	void setOver(bool over);
	void updateDownValueFromPos(int pos);

	int lineLeft() const;
	int lineWidth() const;

	const style::MediaSlider &_st;

	bool _disabled = false;

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
