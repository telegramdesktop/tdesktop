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
#include "localimageloader.h"

class PhotoSendBox : public AbstractBox {
	Q_OBJECT

public:

	PhotoSendBox(const ReadyLocalMedia &img);
	PhotoSendBox(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	~PhotoSendBox();

public slots:

	void onSend(bool ctrlShiftEnter = false);

protected:

	void closePressed();
	void hideAll();
	void showAll();

private:

	ReadyLocalMedia *_img;
	int32 _thumbx, _thumby, _thumbw, _thumbh;
	QString _name, _size;
	int32 _namew, _textw;
	FlatCheckbox _compressed;
	FlatButton _sendButton, _cancelButton;
	QPixmap _thumb;

	QString _phone, _fname, _lname;
	MsgId _replyTo;


};
