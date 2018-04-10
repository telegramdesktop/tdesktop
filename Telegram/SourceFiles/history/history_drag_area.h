/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	void setDroppedCallback(base::lambda<void(const QMimeData *data)> callback) {
		_droppedCallback = std::move(callback);
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
