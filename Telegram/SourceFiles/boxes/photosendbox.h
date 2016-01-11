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

#include "abstractbox.h"
#include "localimageloader.h"

class PhotoSendBox : public AbstractBox {
	Q_OBJECT

public:

	PhotoSendBox(const FileLoadResultPtr &file);
	PhotoSendBox(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		if (_caption.isHidden()) {
			setFocus();
		} else {
			_caption.setFocus();
		}
	}

signals:

	void confirmed();
	void cancelled();

public slots:

	void onCompressedChange();
	void onCaptionResized();
	void onSend(bool ctrlShiftEnter = false);

protected:

	void closePressed();
	void hideAll();
	void showAll();
	void showDone();

private:

	void updateBoxSize();

	FileLoadResultPtr _file;
	bool _animated;

	QPixmap _thumb;

	InputArea _caption;
	bool _compressedFromSettings;
	Checkbox _compressed;
	BoxButton _send, _cancel;

	int32 _thumbx, _thumby, _thumbw, _thumbh;
	Text _name;
	QString _status;
	int32 _statusw;
	bool _isImage;

	QString _phone, _fname, _lname;

	MsgId _replyTo;

	bool _confirmed;

};
