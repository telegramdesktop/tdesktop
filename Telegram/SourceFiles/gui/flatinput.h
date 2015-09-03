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

#include <QtWidgets/QLineEdit>
#include "style.h"
#include "animation.h"

class FlatInput : public QLineEdit, public Animated {
	Q_OBJECT

public:

	FlatInput(QWidget *parent, const style::flatInput &st, const QString &ph = QString(), const QString &val = QString());

	bool event(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void resizeEvent(QResizeEvent *e);

	void notaBene();

	void setPlaceholder(const QString &ph);
	void setPlaceholderFast(bool fast);
	void updatePlaceholder();
	const QString &placeholder() const;
	QRect placeholderRect() const;

	QRect getTextRect() const;

	bool animStep(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	void customUpDown(bool isCustom);
	QString getLastText() const {
		return text();
	}

public slots:

	void onTextChange(const QString &text);
	void onTextEdited();

	void onTouchTimer();

signals:

	void changed();
	void cancelled();
	void accepted();
	void focused();
	void blurred();

protected:

	virtual void correctValue(QKeyEvent *e, const QString &was);

	style::font phFont() {
		return _st.font;
	}

	void phPrepare(Painter &p);

private:

	QString _ph, _fullph, _oldtext;
	bool _fastph;
	QKeyEvent *_kev;

	bool _customUpDown;

	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor, a_borderColor, a_bgColor;

	int _notingBene;
	style::flatInput _st;

	style::font _font;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

class CountryCodeInput : public FlatInput {
	Q_OBJECT

public:

	CountryCodeInput(QWidget *parent, const style::flatInput &st);

	public slots:

	void startErasing(QKeyEvent *e);
	void codeSelected(const QString &code);

signals:

	void codeChanged(const QString &code);
	void addedToNumber(const QString &added);

protected:

	void correctValue(QKeyEvent *e, const QString &was);

private:

	bool _nosignal;

};

class InputField : public TWidget {
	Q_OBJECT

public:

	InputField(QWidget *parent, const style::InputField &st, const QString &ph = QString(), const QString &val = QString());

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);
	void resizeEvent(QResizeEvent *e);

	const QString &getLastText() const;
	void updatePlaceholder();

	int32 fakeMargin() const;

	bool placeholderFgStep(float64 ms);
	bool placeholderShiftStep(float64 ms);
	bool borderStep(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner.setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner.textCursor();
	}
	void setText(const QString &text) {
		return _inner.setText(text);
	}
	void clear() {
		return _inner.clear();
	}


public slots:

	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

signals:

	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();

protected:

	virtual void correctValue(QKeyEvent *e, const QString &was);
	
	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

private:

	friend class InputFieldInner;
	class InputFieldInner : public QTextEdit {
	public:
		InputFieldInner(InputField *parent, const QString &val = QString());

		bool viewportEvent(QEvent *e);
		void focusInEvent(QFocusEvent *e);
		void focusOutEvent(QFocusEvent *e);
		void keyPressEvent(QKeyEvent *e);
		void paintEvent(QPaintEvent *e);

		QMimeData *createMimeDataFromSelection() const;

		QVariant loadResource(int type, const QUrl &name);

	private:

		InputField *f() const {
			return static_cast<InputField*>(parentWidget());
		}
		friend class InputField;
	};
	InputFieldInner _inner;
	void focusInInner();
	void focusOutInner();

	void processDocumentContentsChange(int position, int charsAdded);

	QString _oldtext;

	QKeyEvent *_keyEvent;

	bool _undoAvailable, _redoAvailable;

	int32 _fakeMargin;
	
	bool _customUpDown;
	
	QString _placeholder, _placeholderFull;
	bool _placeholderVisible;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _placeholderFgAnim, _placeholderShiftAnim;
	
	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _borderAnim;
	
	bool _focused, _error;
	
	const style::InputField *_st;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _replacingEmojis;
	typedef QPair<int, int> Insertion;
	typedef QList<Insertion> Insertions;
	Insertions _insertions;
};
