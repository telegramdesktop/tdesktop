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

#include <QtWidgets/QTextEdit>
#include "style.h"
#include "animation.h"

class FlatTextarea : public QTextEdit, public Animated {
	Q_OBJECT

public:

	FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &ph = QString(), const QString &val = QString());

	bool viewportEvent(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);

	const QString &getLastText() const;
	void updatePlaceholder();

	QRect getTextRect() const;
	int32 fakeMargin() const;

	bool animStep(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	EmojiPtr getSingleEmoji() const;
	void getMentionHashtagBotCommandStart(QString &start) const;
	void removeSingleEmoji();
	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void parseLinks();
	QStringList linksList() const;

	void insertFromMimeData(const QMimeData *source);

public slots:

	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onMentionHashtagOrBotCommandInsert(QString str);

signals:

	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();
	void spacedReturnedPasted();
	void linksChanged();

protected:

	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}
	void enterEvent(QEvent *e) {
		TWidget *p(tparent());
		if (p) p->leaveToChildEvent(e);
		return QTextEdit::enterEvent(e);
	}
	void leaveEvent(QEvent *e) {
		TWidget *p(tparent());
		if (p) p->enterFromChildEvent(e);
		return QTextEdit::leaveEvent(e);
	}

	QVariant loadResource(int type, const QUrl &name);

private:

	void getSingleEmojiFragment(QString &text, QTextFragment &fragment) const;
	void processDocumentContentsChange(int position, int charsAdded);

	QMimeData *createMimeDataFromSelection() const;

	QString _ph, _phelided, _oldtext;
	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor;
	style::flatTextarea _st;

	bool _undoAvailable, _redoAvailable;

	int32 _fakeMargin;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _replacingEmojis;
	typedef QPair<int, int> Insertion;
	typedef QList<Insertion> Insertions;
	Insertions _insertions;

	typedef QPair<int, int> LinkRange;
	typedef QList<LinkRange> LinkRanges;
	LinkRanges _links;
};
