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

#include "boxes/abstract_box.h"

namespace Ui {
class UsernameInput;
class LinkButton;
} // namespace Ui

class UsernameBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	UsernameBox(QWidget*);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSave();

	void onCheck();
	void onChanged();

	void onLinkClick();

private:
	void onUpdateDone(const MTPUser &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);

	QString getName() const;
	void updateLinkText();

	object_ptr<Ui::UsernameInput> _username;
	object_ptr<Ui::LinkButton> _link;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	Text _about;
	object_ptr<QTimer> _checkTimer;

};
