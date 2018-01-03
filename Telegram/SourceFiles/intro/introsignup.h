/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/introwidget.h"

namespace Ui {
class RoundButton;
class InputField;
class UserpicButton;
} // namespace Ui

namespace Intro {

class SignupWidget : public Widget::Step {
	Q_OBJECT

public:
	SignupWidget(QWidget *parent, Widget::Data *data);

	void setInnerFocus() override;
	void activate() override;
	void cancelled() override;
	void submit() override;
	QString nextButtonText() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onInputChange();
	void onCheckRequest();

private:
	void refreshLang();
	void updateControlsGeometry();

	void nameSubmitDone(const MTPauth_Authorization &result);
	bool nameSubmitFail(const RPCError &error);

	void stopCheck();

	object_ptr<Ui::UserpicButton> _photo;
	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;
	QString _firstName, _lastName;
	mtpRequestId _sentRequest = 0;

	bool _invertOrder = false;

	object_ptr<QTimer> _checkRequest;

};

} // namespace Intro
