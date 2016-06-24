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

#include <QtWidgets/QWidget>
#include "ui/flatbutton.h"
#include "ui/flatinput.h"
#include "intro/introwidget.h"

class IntroSignup final : public IntroStep {
	Q_OBJECT

public:

	IntroSignup(IntroWidget *parent);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	void step_error(float64 ms, bool timer);
	void step_photo(float64 ms, bool timer);

	void activate() override;
	void cancelled() override;
	void onSubmit() override;

	void nameSubmitDone(const MTPauth_Authorization &result);
	bool nameSubmitFail(const RPCError &error);

public slots:

	void onSubmitName(bool force = false);
	void onInputChange();
	void onCheckRequest();
	void onPhotoReady(const QImage &img);

private:

	void showError(const QString &err);
	void stopCheck();

	QString error;
	anim::fvalue a_errorAlpha, a_photoOver;
	Animation _a_error;
	Animation _a_photo;

	FlatButton next;

	QRect textRect;

	bool _photoOver;
	QImage _photoBig;
	QPixmap _photoSmall;
	int32 _phLeft, _phTop;

	FlatInput first, last;
	QString firstName, lastName;
	mtpRequestId sentRequest;

	bool _invertOrder;

	QTimer checkRequest;
};
