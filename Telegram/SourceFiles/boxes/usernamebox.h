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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"

class UsernameInput : public FlatInput {
public:

	UsernameInput(QWidget *parent, const style::flatInput &st, const QString &ph = QString(), const QString &val = QString());

protected:

	void correctValue(QKeyEvent *e, const QString &was);

};

class UsernameBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	UsernameBox();
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onSave();
	
	void onCheck();
	void onChanged();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void onUpdateDone(const MTPUser &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);

	QString getName() const;
	void initBox();

	FlatButton _saveButton, _cancelButton;
	UsernameInput _usernameInput;

	mtpRequestId _saveRequest, _checkRequest;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	Text _about;
	QTimer _checkTimer;
};
