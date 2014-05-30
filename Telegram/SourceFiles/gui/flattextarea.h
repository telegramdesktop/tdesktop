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

#include <QtWidgets/QTextEdit>
#include "style.h"
#include "animation.h"

class FlatTextarea : public QTextEdit, public Animated {
	Q_OBJECT

public:

	FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &ph = QString(), const QString &val = QString());
	QString val() const;

	bool viewportEvent(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);

	void updatePlaceholder();

	QRect getTextRect() const;

	bool animStep(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

public slots:

	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

signals:

	void changed();
	void submitted();
	void cancelled();
	void tabbed();

protected:

	void insertEmoji(EmojiPtr emoji, QTextCursor c);

private:

	void processDocumentContentsChange(int position, int charsAdded);

	QMimeData *createMimeDataFromSelection() const;

	QString _ph, _phelided, _oldtext;
	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor;
	style::flatTextarea _st;

	int32 _fakeMargin;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _replacingEmojis;
	typedef QPair<int, int> Insertion;
	typedef QList<Insertion> Insertions;
	Insertions _insertions;
};
