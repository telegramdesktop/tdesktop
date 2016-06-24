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

#include "mtproto/rpc_sender.h"

class IntroStep;
class IntroWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	IntroWidget(QWidget *window);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void updateAdaptiveLayout();

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	void step_show(float64 ms, bool timer);
	void stop_show();

	void step_stage(float64 ms, bool timer);

	QRect innerRect() const;
	QString currentCountry() const;

	enum CallStatusType {
		CallWaiting,
		CallCalling,
		CallCalled,
		CallDisabled,
	};
	struct CallStatus {
		CallStatusType type;
		int timeout;
	};
	void setPhone(const QString &phone, const QString &phone_hash, bool registered);
	void setCode(const QString &code);
	void setCallStatus(const CallStatus &status);
	void setPwdSalt(const QByteArray &salt);
	void setHasRecovery(bool hasRecovery);
	void setPwdHint(const QString &hint);
	void setCodeByTelegram(bool byTelegram);

	const QString &getPhone() const;
	const QString &getPhoneHash() const;
	const QString &getCode() const;
	const CallStatus &getCallStatus() const;
	const QByteArray &getPwdSalt() const;
	bool getHasRecovery() const;
	const QString &getPwdHint() const;
	bool codeByTelegram() const;

	void finish(const MTPUser &user, const QImage &photo = QImage());

	void rpcClear() override;
	void langChangeTo(int32 langId);

	void nextStep(IntroStep *step) {
		pushStep(step, MoveForward);
	}
	void replaceStep(IntroStep *step) {
		pushStep(step, MoveReplace);
	}

	~IntroWidget() override;

public slots:

	void onStepSubmit();
	void onBack();
	void onParentResize(const QSize &newSize);
	void onChangeLang();

signals:

	void countryChanged();

private:

	QPixmap grabStep(int skip = 0);

	int _langChangeTo = 0;

	Animation _a_stage;
	QPixmap _cacheHide, _cacheShow;
	int _cacheHideIndex = 0;
	int _cacheShowIndex = 0;
	anim::ivalue a_coordHide, a_coordShow;
	anim::fvalue a_opacityHide, a_opacityShow;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	QVector<IntroStep*> _stepHistory;
	IntroStep *step(int skip = 0) {
		t_assert(_stepHistory.size() + skip > 0);
		return _stepHistory.at(_stepHistory.size() - skip - 1);
	}
	enum MoveType {
		MoveBack,
		MoveForward,
		MoveReplace,
	};
	void historyMove(MoveType type);
	void pushStep(IntroStep *step, MoveType type);

	void gotNearestDC(const MTPNearestDc &dc);

	QString _countryForReg;

	QString _phone, _phone_hash;
	CallStatus _callStatus = { CallDisabled, 0 };
	bool _registered = false;

	QString _code;

	QByteArray _pwdSalt;
	bool _hasRecovery = false;
	bool _codeByTelegram = false;
	QString _pwdHint;

	QString _firstname, _lastname;

	IconedButton _back;
	float64 _backFrom = 0.;
	float64 _backTo = 0.;

};

class IntroStep : public TWidget, public RPCSender {
public:

	IntroStep(IntroWidget *parent) : TWidget(parent) {
	}

	virtual bool hasBack() const {
		return false;
	}
	virtual void activate() {
		show();
	}
	virtual void cancelled() {
	}
	virtual void finished() {
		hide();
	}
	virtual void onSubmit() = 0;

protected:

	IntroWidget *intro() {
		IntroWidget *result = qobject_cast<IntroWidget*>(parentWidget());
		t_assert(result != nullptr);
		return result;
	}

};
