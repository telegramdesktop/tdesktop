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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
