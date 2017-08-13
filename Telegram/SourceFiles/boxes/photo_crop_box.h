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

class PhotoCropBox : public BoxContent {
	Q_OBJECT

public:
	PhotoCropBox(QWidget*, const QImage &img, const PeerId &peer);
	PhotoCropBox(QWidget*, const QImage &img, PeerData *peer);

	int32 mouseState(QPoint p);

signals:
	void ready(const QImage &tosend);

private slots:
	void onReady(const QImage &tosend);

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void init(const QImage &img, PeerData *peer);
	void sendPhoto();

	QString _title;
	int32 _downState = 0;
	int32 _thumbx, _thumby, _thumbw, _thumbh;
	int32 _cropx, _cropy, _cropw;
	int32 _fromposx, _fromposy, _fromcropx, _fromcropy, _fromcropw;
	QImage _img;
	QPixmap _thumb;
	QImage _mask, _fade;
	PeerId _peerId;

};
