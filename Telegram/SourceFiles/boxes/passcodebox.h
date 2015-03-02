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

#include "layerwidget.h"

class PasscodeBox : public LayeredWidget {
	Q_OBJECT

public:

	PasscodeBox(bool turningOff = false);
	void parentResized();
	void animStep(float64 dt);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();
	~PasscodeBox();

public slots:

	void onSave();
	void onBadOldPasscode();
	void onOldChanged();
	void onNewChanged();
	void onCancel();

private:

	void hideAll();
	void showAll();

	bool _turningOff;

	QString _boxTitle;
	Text _about;

	int32 _width, _height;
	FlatButton _saveButton, _cancelButton;
	FlatInput _oldPasscode, _newPasscode, _reenterPasscode;

	QPixmap _cache;

	anim::fvalue a_opacity;
	bool _hiding;

	QTimer _badOldTimer;
	QString _oldError, _newError;
};
