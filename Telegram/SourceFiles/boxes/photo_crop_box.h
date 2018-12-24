/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

class PhotoCropBox : public BoxContent {
public:
	PhotoCropBox(QWidget*, const QImage &img, const QString &title);

	int32 mouseState(QPoint p);

	rpl::producer<QImage> ready() const;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void sendPhoto();

	QString _title;
	int32 _downState = 0;
	int32 _thumbx, _thumby, _thumbw, _thumbh;
	int32 _cropx, _cropy, _cropw;
	int32 _fromposx, _fromposy, _fromcropx, _fromcropy, _fromcropw;
	QImage _img;
	QPixmap _thumb;
	QImage _mask, _fade;
	rpl::event_stream<QImage> _readyImages;

};
