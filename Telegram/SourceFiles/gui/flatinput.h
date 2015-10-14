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

#include "style.h"
#include "animation.h"

class FlatInput : public QLineEdit, public Animated {
	Q_OBJECT
	T_WIDGET

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
	const QString &getLastText() const {
		return _oldtext;
	}

	void setTextMargins(const QMargins &mrg);

public slots:

	void onTextChange(const QString &text);
	void onTextEdited();

	void onTouchTimer();

signals:

	void changed();
	void cancelled();
	void submitted(bool ctrlShiftEnter);
	void focused();
	void blurred();

protected:

	virtual void correctValue(const QString &was, QString &now);

	style::font phFont() {
		return _st.font;
	}

	void phPrepare(Painter &p);

private:

	QString _oldtext, _ph, _fullph;
	bool _fastph;

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

	void correctValue(const QString &was, QString &now);

private:

	bool _nosignal;

};

class PhonePartInput : public FlatInput {
	Q_OBJECT

public:

	PhonePartInput(QWidget *parent, const style::flatInput &st);

	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);

public slots:

	void addedToNumber(const QString &added);
	void onChooseCode(const QString &code);

signals:

	void voidBackspace(QKeyEvent *e);

protected:

	void correctValue(const QString &was, QString &now);

private:

	QVector<int> pattern;

};

class InputArea : public TWidget {
	Q_OBJECT

public:

	InputArea(QWidget *parent, const style::InputArea &st, const QString &ph = QString(), const QString &val = QString());

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);
	void resizeEvent(QResizeEvent *e);

	void showError();

	void setMaxLength(int32 maxLength) {
		_maxLength = maxLength;
	}

	const QString &getLastText() const {
		return _oldtext;
	}
	void updatePlaceholder();

	bool animStep_placeholderFg(float64 ms);
	bool animStep_placeholderShift(float64 ms);
	bool animStep_border(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);
	void setCtrlEnterSubmit(bool ctrlEnterSubmit);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner.setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner.textCursor();
	}
	void setText(const QString &text) {
		_inner.setText(text);
		updatePlaceholder();
	}
	void clear() {
		_inner.clear();
		updatePlaceholder();
	}
	bool hasFocus() const {
		return _inner.hasFocus();
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
	void resized();

protected:

	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

private:

	int32 _maxLength;
	bool heightAutoupdated();
	void checkContentHeight();

	friend class InputAreaInner;
	class InputAreaInner : public QTextEdit {
	public:
		InputAreaInner(InputArea *parent);

		bool viewportEvent(QEvent *e);
		void focusInEvent(QFocusEvent *e);
		void focusOutEvent(QFocusEvent *e);
		void keyPressEvent(QKeyEvent *e);
		void paintEvent(QPaintEvent *e);

		QMimeData *createMimeDataFromSelection() const;

		QVariant loadResource(int type, const QUrl &name);

	private:

		InputArea *f() const {
			return static_cast<InputArea*>(parentWidget());
		}
		friend class InputArea;
	};

	void focusInInner();
	void focusOutInner();

	void processDocumentContentsChange(int position, int charsAdded);

	void startBorderAnimation();

	InputAreaInner _inner;

	QString _oldtext;

	bool _undoAvailable, _redoAvailable, _inHeightCheck, _ctrlEnterSubmit;

	bool _customUpDown;

	QString _placeholder, _placeholderFull;
	bool _placeholderVisible;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;

	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;

	bool _focused, _error;

	const style::InputArea &_st;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _correcting;
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

	void setMaxLength(int32 maxLength) {
		_maxLength = maxLength;
	}

	void showError();

	const QString &getLastText() const {
		return _oldtext;
	}
	void updatePlaceholder();

	bool animStep_placeholderFg(float64 ms);
	bool animStep_placeholderShift(float64 ms);
	bool animStep_border(float64 ms);

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
		_inner.setText(text);
		updatePlaceholder();
	}
	void clear() {
		_inner.clear();
		updatePlaceholder();
	}
	bool hasFocus() const {
		return _inner.hasFocus();
	}
	void setFocus() {
		_inner.setFocus();
		QTextCursor c(_inner.textCursor());
		c.movePosition(QTextCursor::End);
		_inner.setTextCursor(c);
	}
	void clearFocus() {
		_inner.clearFocus();
	}
	void setCursorPosition(int pos) {
		QTextCursor c(_inner.textCursor());
		c.setPosition(pos);
		_inner.setTextCursor(c);
	}

public slots:

	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void selectAll();

signals:

	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();

protected:

	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

private:

	int32 _maxLength;

	friend class InputFieldInner;
	class InputFieldInner : public QTextEdit {
	public:
		InputFieldInner(InputField *parent);

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

	void focusInInner();
	void focusOutInner();

	void processDocumentContentsChange(int position, int charsAdded);

	void startBorderAnimation();

	InputFieldInner _inner;

	QString _oldtext;

	bool _undoAvailable, _redoAvailable;

	bool _customUpDown;
	
	QString _placeholder, _placeholderFull;
	bool _placeholderVisible;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;
	
	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;
	
	bool _focused, _error;
	
	const style::InputField &_st;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _correcting;
};

class MaskedInputField : public QLineEdit {
	Q_OBJECT
	T_WIDGET

public:

	MaskedInputField(QWidget *parent, const style::InputField &st, const QString &placeholder = QString(), const QString &val = QString());

	bool event(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void focusInEvent(QFocusEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void resizeEvent(QResizeEvent *e);

	void showError();

	bool setPlaceholder(const QString &ph);
	void setPlaceholderFast(bool fast);
	void updatePlaceholder();

	QRect getTextRect() const;

	bool animStep_placeholderFg(float64 ms);
	bool animStep_placeholderShift(float64 ms);
	bool animStep_border(float64 ms);

	QSize sizeHint() const;
	QSize minimumSizeHint() const;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}
	void setText(const QString &text) {
		QLineEdit::setText(text);
		updatePlaceholder();
	}
	void clear() {
		QLineEdit::clear();
		updatePlaceholder();
	}

public slots:

	void onTextChange(const QString &text);
	void onCursorPositionChanged(int oldPosition, int position);

	void onTextEdited();

	void onTouchTimer();

signals:

	void changed();
	void cancelled();
	void submitted(bool ctrlShiftEnter);
	void focused();
	void blurred();

protected:

	virtual void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor);
	virtual void paintPlaceholder(Painter &p);

	style::font phFont() {
		return _st.font;
	}

	void placeholderPreparePaint(Painter &p);
	const QString &placeholder() const;
	QRect placeholderRect() const;

	void setTextMargins(const QMargins &mrg);
	const style::InputField &_st;

private:

	void startBorderAnimation();

	int32 _maxLength;

	QString _oldtext;
	int32 _oldcursor;

	bool _undoAvailable, _redoAvailable;

	bool _customUpDown;

	QString _placeholder, _placeholderFull;
	bool _placeholderVisible, _placeholderFast;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;

	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;

	bool _focused, _error;

	style::margins _textMargins;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

class PasswordField : public MaskedInputField {
public:

	PasswordField(QWidget *parent, const style::InputField &st, const QString &ph = QString(), const QString &val = QString());

};

class PortInput : public MaskedInputField {
public:

	PortInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val);

protected:

	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor);

};

class UsernameInput : public MaskedInputField {
public:

	UsernameInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val, bool isLink);
	void paintPlaceholder(Painter &p);

protected:

	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor);

private:

	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:

	PhoneInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val);

	void focusInEvent(QFocusEvent *e);
	void clearText();

protected:

	void paintPlaceholder(Painter &p);
	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor);

private:

	QString _defaultPlaceholder;
	QVector<int> pattern;

};
