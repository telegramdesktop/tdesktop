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

#include "ui/twidget.h"

class DragArea : public TWidget {
	Q_OBJECT

public:
	DragArea(QWidget *parent);

	void setText(const QString &text, const QString &subtext);

	void otherEnter();
	void otherLeave();

	bool overlaps(const QRect &globalRect);

	void hideFast();

	void setDroppedCallback(base::lambda<void(const QMimeData *data)> &&callback) {
		_droppedCallback = std_::move(callback);
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;

public slots:
	void hideStart();
	void hideFinish();

	void showStart();

private:
	void setIn(bool in);
	void opacityAnimationCallback();
	QRect innerRect() const {
		return QRect(
			st::dragPadding.left(),
			st::dragPadding.top(),
			width() - st::dragPadding.left() - st::dragPadding.right(),
			height() - st::dragPadding.top() - st::dragPadding.bottom()
		);
	}

	bool _hiding = false;
	bool _in = false;
	QPixmap _cache;
	base::lambda<void(const QMimeData *data)> _droppedCallback;

	Animation _a_opacity;
	Animation _a_in;

	QString _text, _subtext;

};
