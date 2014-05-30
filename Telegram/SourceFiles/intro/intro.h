/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include <QtWidgets/QWidget>
#include "gui/flatbutton.h"

class Window;
class IntroSteps;
class IntroPhone;
class IntroCode;
class IntroSignup;
class IntroStage;
class Text;

class IntroWidget : public QWidget, public Animated {
	Q_OBJECT

public:

	IntroWidget(Window *window);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	bool animStep(float64 ms);

	QRect innerRect() const;
	QString currentCountry() const;

	void setPhone(const QString &phone, const QString &phone_hash, bool registered);
	void setCode(const QString &code);
	void setCallTimeout(int32 callTimeout);

	const QString &getPhone() const;
	const QString &getPhoneHash() const;
	const QString &getCode() const;
	int32 getCallTimeout() const;

	void finish(const MTPUser &user, const QImage &photo = QImage());

	~IntroWidget();

public slots:

	void onIntroNext();
	void onIntroBack();
	void onDoneStateChanged(int oldState, ButtonStateChangeSource source);
	void onParentResize(const QSize &newSize);

signals:

	void countryChanged();

private:

	void makeHideCache(int stage = -1);
	void makeShowCache(int stage = -1);
	void prepareMove();
	bool createNext();

	QPixmap cacheForHide, cacheForShow;
	int cacheForHideInd, cacheForShowInd;
	anim::ivalue xCoordHide, xCoordShow;
	anim::fvalue cAlphaHide, cAlphaShow;

	QPixmap _animCache, _bgAnimCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	Window *wnd;
	IntroSteps *steps;
	IntroPhone *phone;
	IntroCode *code;
	IntroSignup *signup;
	IntroStage *stages[4];
	int current, moving, visibilityChanging;

	QString _phone, _phone_hash;
	int32 _callTimeout;
	bool _registered;

	QString _code;

	QString _firstname, _lastname;

};

class IntroStage : public QWidget {
public:

	IntroStage(IntroWidget *parent) : QWidget(parent) {
	}

	virtual void activate() = 0; // show and activate
	virtual void deactivate() = 0; // deactivate and hide
	virtual void onNext() = 0;
	virtual void onBack() = 0;

protected:
	
	IntroWidget *intro() {
		return qobject_cast<IntroWidget*>(parent());
	}

};
