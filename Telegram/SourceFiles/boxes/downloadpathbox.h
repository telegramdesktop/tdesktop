/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "layerwidget.h"
#include "gui/phoneinput.h"

class DownloadPathBox : public LayeredWidget {
	Q_OBJECT

public:

	DownloadPathBox();
	void parentResized();
	void animStep(float64 dt);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();
	~DownloadPathBox();

public slots:

	void onChange();
	void onEditPath();
	void onSave();
	void onCancel();

private:

	void hideAll();
	void showAll();

	QString _path;

	FlatRadiobutton _tempRadio, _dirRadio;
	FlatInput _dirInput;
	FlatButton _saveButton, _cancelButton;

	int32 _width, _height;
	QPixmap _cache;

	anim::fvalue a_opacity;
	bool _hiding;
};
