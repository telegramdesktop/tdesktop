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
#include "gui/twidget.h"

typedef enum {
	ButtonByUser  = 0x00, // by clearState() call
	ButtonByPress = 0x01,
	ButtonByHover = 0x02,
} ButtonStateChangeSource;

class Button : public TWidget {
	Q_OBJECT

public:
	Button(QWidget *parent);

	enum {
		StateNone     = 0x00,
		StateOver     = 0x01,
		StateDown     = 0x02,
		StateDisabled = 0x04,
	};

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	Qt::KeyboardModifiers clickModifiers() const {
		return _modifiers;
	}

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void clearState();
	int getState() const;

	void setDisabled(bool disabled = true);
	void setOver(bool over, ButtonStateChangeSource source = ButtonByUser);
	bool disabled() const {
		return (_state & StateDisabled);
	}

	void setAcceptBoth(bool acceptBoth = true);

signals:

	void clicked();
	void stateChanged(int oldState, ButtonStateChangeSource source);

protected:

	Qt::KeyboardModifiers _modifiers;
	int _state;
	bool _acceptBoth;

};
