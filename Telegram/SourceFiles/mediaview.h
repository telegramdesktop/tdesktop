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

class MediaView : public QWidget {
	Q_OBJECT

public:

	MediaView();

	void paintEvent(QPaintEvent *e);
	
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);

	void showPhoto(PhotoData *photo, const QRect &opaque);
	void moveToScreen();

private:

	QTimer _timer;
	PhotoData *_photo;
	QRect _opaqueRect;

	int32 _maxWidth, _maxHeight, _x, _y, _w;

};
