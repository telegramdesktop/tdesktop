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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <QtWidgets/QTextEdit>
#include "style.h"
#include "animation.h"

class UserData;
class FlatTextarea : public QTextEdit {
	Q_OBJECT
	T_WIDGET

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
	void dropEvent(QDropEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void setMaxLength(int32 maxLength);
	void setMinHeight(int32 minHeight);
	void setMaxHeight(int32 maxHeight);

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(const QString &ph, int32 afterSymbols = 0);
	void updatePlaceholder();
	void finishPlaceholder();

	QRect getTextRect() const;
	int32 fakeMargin() const;

	void step_appearance(float64 ms, bool timer);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	EmojiPtr getSingleEmoji() const;
	QString getMentionHashtagBotCommandPart(bool &start) const;
	QString getInlineBotQuery(UserData *&contextBot, QString &contextBotUsername) const;
	void removeSingleEmoji();
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void parseLinks();
	QStringList linksList() const;

	void insertFromMimeData(const QMimeData *source);

	QMimeData *createMimeDataFromSelection() const;
	void setCtrlEnterSubmit(bool ctrlEnterSubmit);

	void setTextFast(const QString &text, bool clearUndoHistory = true);

public slots:

	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onMentionHashtagOrBotCommandInsert(QString str);

signals:

	void resized();
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();
	void spacedReturnedPasted();
	void linksChanged();

protected:

	QString getText(int32 start = 0, int32 end = -1) const;
	virtual void correctValue(const QString &was, QString &now);

	void insertEmoji(EmojiPtr emoji, QTextCursor c);

	QVariant loadResource(int type, const QUrl &name);

	void checkContentHeight();

private:

	void getSingleEmojiFragment(QString &text, QTextFragment &fragment) const;
	void processDocumentContentsChange(int position, int charsAdded);
	bool heightAutoupdated();

	int32 _minHeight, _maxHeight; // < 0 - no autosize
	int32 _maxLength;
	bool _ctrlEnterSubmit;

	QString _ph, _phelided, _oldtext;
	int32 _phAfter;
	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor;
	Animation _a_appearance;

	style::flatTextarea _st;

	bool _undoAvailable, _redoAvailable, _inDrop, _inHeightCheck;

	int32 _fakeMargin;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _correcting;

	typedef QPair<int, int> LinkRange;
	typedef QList<LinkRange> LinkRanges;
	LinkRanges _links;
};
