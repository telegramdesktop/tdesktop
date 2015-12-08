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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

class Window;
class IntroSteps;
class IntroPhone;
class IntroCode;
class IntroSignup;
class IntroPwdCheck;
class IntroStage;
class Text;

class IntroWidget : public TWidget {
	Q_OBJECT

public:

	IntroWidget(Window *window);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);
	
	void updateWideMode();

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	void step_show(float64 ms, bool timer);
	void stop_show();
		
	void step_stage(float64 ms, bool timer);

	QRect innerRect() const;
	QString currentCountry() const;

	void setPhone(const QString &phone, const QString &phone_hash, bool registered);
	void setCode(const QString &code);
	void setCallTimeout(int32 callTimeout);
	void setPwdSalt(const QByteArray &salt);
	void setHasRecovery(bool hasRecovery);
	void setPwdHint(const QString &hint);
	void setCodeByTelegram(bool byTelegram);

	const QString &getPhone() const;
	const QString &getPhoneHash() const;
	const QString &getCode() const;
	int32 getCallTimeout() const;
	const QByteArray &getPwdSalt() const;
	bool getHasRecovery() const;
	const QString &getPwdHint() const;
	bool codeByTelegram() const;

	void finish(const MTPUser &user, const QImage &photo = QImage());

	void rpcInvalidate();
	void langChangeTo(int32 langId);

	~IntroWidget();

public slots:

	void onIntroNext();
	void onIntroBack();
	void onDoneStateChanged(int oldState, ButtonStateChangeSource source);
	void onParentResize(const QSize &newSize);
	void onChangeLang();

signals:

	void countryChanged();

private:

	void makeHideCache(int stage = -1);
	void makeShowCache(int stage = -1);
	void prepareMove();
	bool createNext();

	int32 _langChangeTo;

	Animation _a_stage;
	QPixmap _cacheHide, _cacheShow;
	int _cacheHideIndex, _cacheShowIndex;
	anim::ivalue a_coordHide, a_coordShow;
	anim::fvalue a_opacityHide, a_opacityShow;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	IntroSteps *steps;
	IntroPhone *phone;
	IntroCode *code;
	IntroSignup *signup;
	IntroPwdCheck *pwdcheck;
	IntroStage *stages[5];
	int current, moving;

	QString _phone, _phone_hash;
	int32 _callTimeout;
	bool _registered;

	QString _code;

	QByteArray _pwdSalt;
	bool _hasRecovery, _codeByTelegram;
	QString _pwdHint;

	QString _firstname, _lastname;

	IconedButton _back;
	float64 _backFrom, _backTo;

};

class IntroStage : public TWidget {
public:

	IntroStage(IntroWidget *parent) : TWidget(parent) {
	}

	virtual void activate() = 0; // show and activate
	virtual void prepareShow() {
	}
	virtual void deactivate() = 0; // deactivate and hide
	virtual void onNext() = 0;
	virtual void onBack() = 0;
	virtual bool hasBack() const {
		return false;
	}

protected:
	
	IntroWidget *intro() {
		return qobject_cast<IntroWidget*>(parent());
	}

};
