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

#include "styles/style_widgets.h"

class UserData;

namespace Ui {

static UserData * const LookingUpInlineBot = SharedMemoryLocation<UserData, 0>();

class FlatTextarea : public TWidgetHelper<QTextEdit>, private base::Subscriber {
	Q_OBJECT

public:
	using TagList = TextWithTags::Tags;

	static QByteArray serializeTagsList(const TagList &tags);
	static TagList deserializeTagsList(QByteArray data, int textLength);
	static QString tagsMimeType();

	FlatTextarea(QWidget *parent, const style::FlatTextarea &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString(), const TagList &tags = TagList());

	void setMaxLength(int maxLength);
	void setMinHeight(int minHeight);
	void setMaxHeight(int maxHeight);

	void setPlaceholder(base::lambda<QString()> placeholderFactory, int afterSymbols = 0);
	void updatePlaceholder();
	void finishPlaceholder();

	QRect getTextRect() const;
	int fakeMargin() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	EmojiPtr getSingleEmoji() const;
	QString getMentionHashtagBotCommandPart(bool &start) const;

	// Get the current inline bot and request string for it.
	// The *outInlineBot can be filled by LookingUpInlineBot shared ptr.
	// In that case the caller should lookup the bot by *outInlineBotUsername.
	QString getInlineBotQuery(UserData **outInlineBot, QString *outInlineBotUsername) const;

	void removeSingleEmoji();
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void parseLinks();
	QStringList linksList() const;

	void insertFromMimeData(const QMimeData *source) override;

	QMimeData *createMimeDataFromSelection() const override;

	enum class SubmitSettings {
		None,
		Enter,
		CtrlEnter,
		Both,
	};
	void setSubmitSettings(SubmitSettings settings);

	const TextWithTags &getTextWithTags() const {
		return _lastTextWithTags;
	}
	TextWithTags getTextWithTagsPart(int start, int end = -1);
	void insertTag(const QString &text, QString tagId = QString());

	bool isEmpty() const {
		return _lastTextWithTags.text.isEmpty();
	}

	enum UndoHistoryAction {
		AddToUndoHistory,
		MergeWithUndoHistory,
		ClearUndoHistory
	};
	void setTextWithTags(const TextWithTags &textWithTags, UndoHistoryAction undoHistoryAction = AddToUndoHistory);

	// If you need to make some preparations of tags before putting them to QMimeData
	// (and then to clipboard or to drag-n-drop object), here is a strategy for that.
	class TagMimeProcessor {
	public:
		virtual QString mimeTagFromTag(const QString &tagId) = 0;
		virtual QString tagFromMimeTag(const QString &mimeTag) = 0;
		virtual ~TagMimeProcessor() {
		}
	};
	void setTagMimeProcessor(std::unique_ptr<TagMimeProcessor> &&processor);

public slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

signals:
	void resized();
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();
	void spacedReturnedPasted();
	void linksChanged();

protected:
	bool viewportEvent(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	virtual void correctValue(const QString &was, QString &now, TagList &nowTags) {
	}

	void insertEmoji(EmojiPtr emoji, QTextCursor c);

	QVariant loadResource(int type, const QUrl &name) override;

	void checkContentHeight();

private:
	void updatePalette();
	void refreshPlaceholder();

	// "start" and "end" are in coordinates of text where emoji are replaced
	// by ObjectReplacementCharacter. If "end" = -1 means get text till the end.
	QString getTextPart(int start, int end, TagList *outTagsList, bool *outTagsChanged = nullptr) const;

	void getSingleEmojiFragment(QString &text, QTextFragment &fragment) const;

	// After any characters added we must postprocess them. This includes:
	// 1. Replacing font family to semibold for ~ characters, if we used Open Sans 13px.
	// 2. Replacing font family from semibold for all non-~ characters, if we used ...
	// 3. Replacing emoji code sequences by ObjectReplacementCharacters with emoji pics.
	// 4. Interrupting tags in which the text was inserted by any char except a letter.
	// 5. Applying tags from "_insertedTags" in case we pasted text with tags, not just text.
	// Rule 4 applies only if we inserted chars not in the middle of a tag (but at the end).
	void processFormatting(int changedPosition, int changedEnd);

	bool heightAutoupdated();

	int placeholderSkipWidth() const;

	int _minHeight = -1; // < 0 - no autosize
	int _maxHeight = -1;
	int _maxLength = -1;
	SubmitSettings _submitSettings = SubmitSettings::Enter;

	QString _placeholder;
	base::lambda<QString()> _placeholderFactory;
	int _placeholderAfterSymbols = 0;
	bool _focused = false;
	bool _placeholderVisible = true;
	Animation _a_placeholderFocused;
	Animation _a_placeholderVisible;

	TextWithTags _lastTextWithTags;

	// Tags list which we should apply while setText() call or insert from mime data.
	TagList _insertedTags;
	bool _insertedTagsAreFromMime;

	// Override insert position and charsAdded from complex text editing
	// (like drag-n-drop in the same text edit field).
	int _realInsertPosition = -1;
	int _realCharsAdded = 0;

	std::unique_ptr<TagMimeProcessor> _tagMimeProcessor;

	const style::FlatTextarea &_st;

	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _inDrop = false;
	bool _inHeightCheck = false;

	int _fakeMargin = 0;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;

	bool _correcting = false;

	struct LinkRange {
		int start;
		int length;
	};
	friend bool operator==(const LinkRange &a, const LinkRange &b);
	friend bool operator!=(const LinkRange &a, const LinkRange &b);
	using LinkRanges = QVector<LinkRange>;
	LinkRanges _links;
};

inline bool operator==(const FlatTextarea::LinkRange &a, const FlatTextarea::LinkRange &b) {
	return (a.start == b.start) && (a.length == b.length);
}
inline bool operator!=(const FlatTextarea::LinkRange &a, const FlatTextarea::LinkRange &b) {
	return !(a == b);
}

class FlatInput : public TWidgetHelper<QLineEdit>, private base::Subscriber {
	Q_OBJECT

public:
	FlatInput(QWidget *parent, const style::FlatInput &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

	void updatePlaceholder();
	void setPlaceholder(base::lambda<QString()> placeholderFactory);
	QRect placeholderRect() const;

	void setTextMrg(const QMargins &textMrg);
	QRect getTextRect() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}

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
	bool event(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	virtual void correctValue(const QString &was, QString &now);

	style::font phFont() {
		return _st.font;
	}

	void phPrepare(Painter &p, float64 placeholderFocused);

private:
	void updatePalette();
	void refreshPlaceholder();

	QString _oldtext;
	QString _placeholder;
	base::lambda<QString()> _placeholderFactory;

	bool _customUpDown = false;

	bool _focused = false;
	bool _placeholderVisible = true;
	Animation _a_placeholderFocused;
	Animation _a_placeholderVisible;
	bool _lastPreEditTextNotEmpty = false;

	const style::FlatInput &_st;
	QMargins _textMrg;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

enum class CtrlEnterSubmit {
	Enter,
	CtrlEnter,
	Both,
};

class InputArea : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	InputArea(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

	void showError();

	void setMaxLength(int maxLength) {
		_maxLength = maxLength;
	}

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(base::lambda<QString()> placeholderFactory);
	void setDisplayFocused(bool focused);
	void finishAnimations();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	QString getText(int start = 0, int end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);
	void setCtrlEnterSubmit(CtrlEnterSubmit ctrlEnterSubmit);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner->setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner->textCursor();
	}
	void setText(const QString &text) {
		_inner->setText(text);
		startPlaceholderAnimation();
	}
	void clear() {
		_inner->clear();
		startPlaceholderAnimation();
	}
	bool hasFocus() const {
		return _inner->hasFocus();
	}
	void setFocus() {
		_inner->setFocus();
	}
	void clearFocus() {
		_inner->clearFocus();
	}

private slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onFocusInner();

signals:
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();
	void resized();

protected:
	void startPlaceholderAnimation();
	void startBorderAnimation();

	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner : public QTextEdit {
	public:
		Inner(InputArea *parent);

		QVariant loadResource(int type, const QUrl &name) override;

	protected:
		bool viewportEvent(QEvent *e) override;
		void focusInEvent(QFocusEvent *e) override;
		void focusOutEvent(QFocusEvent *e) override;
		void keyPressEvent(QKeyEvent *e) override;
		void contextMenuEvent(QContextMenuEvent *e) override;

		QMimeData *createMimeDataFromSelection() const override;

	private:
		InputArea *f() const {
			return static_cast<InputArea*>(parentWidget());
		}
		friend class InputArea;

	};
	friend class Inner;

	void updatePalette();
	void refreshPlaceholder();

	bool heightAutoupdated();
	void checkContentHeight();
	void setErrorShown(bool error);

	void focusInInner(bool focusByMouse);
	void focusOutInner();
	void setFocused(bool focused);

	void processDocumentContentsChange(int position, int charsAdded);

	const style::InputField &_st;

	int _maxLength = -1;

	object_ptr<Inner> _inner;

	QString _oldtext;

	CtrlEnterSubmit _ctrlEnterSubmit = CtrlEnterSubmit::CtrlEnter;
	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _inHeightCheck = false;

	bool _customUpDown = false;

	QString _placeholder;
	base::lambda<QString()> _placeholderFactory;
	Animation _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animation _a_borderShown;
	int _borderAnimationStart = 0;
	Animation _a_borderOpacity;
	bool _borderVisible = false;

	Animation _a_focused;
	Animation _a_error;

	bool _focused = false;
	bool _error = false;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;

	bool _correcting = false;

};

class InputField : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	InputField(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

	void setMaxLength(int maxLength) {
		_maxLength = maxLength;
	}

	void showError();

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(base::lambda<QString()> placeholderFactory);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimations();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	QString getText(int start = 0, int end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner->setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner->textCursor();
	}
	void setText(const QString &text) {
		_inner->setText(text);
		startPlaceholderAnimation();
	}
	void clear() {
		_inner->clear();
		startPlaceholderAnimation();
	}
	bool hasFocus() const {
		return _inner->hasFocus();
	}
	void setFocus() {
		_inner->setFocus();
		auto cursor = _inner->textCursor();
		cursor.movePosition(QTextCursor::End);
		_inner->setTextCursor(cursor);
	}
	void clearFocus() {
		_inner->clearFocus();
	}
	void setCursorPosition(int pos) {
		auto cursor = _inner->textCursor();
		cursor.setPosition(pos);
		_inner->setTextCursor(cursor);
	}

public slots:
	void selectAll();

private slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onFocusInner();

signals:
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();

protected:
	void startPlaceholderAnimation();
	void startBorderAnimation();

	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner : public QTextEdit {
	public:
		Inner(InputField *parent);

		QVariant loadResource(int type, const QUrl &name) override;

	protected:
		bool viewportEvent(QEvent *e) override;
		void focusInEvent(QFocusEvent *e) override;
		void focusOutEvent(QFocusEvent *e) override;
		void keyPressEvent(QKeyEvent *e) override;
		void contextMenuEvent(QContextMenuEvent *e) override;

		QMimeData *createMimeDataFromSelection() const override;

	private:
		InputField *f() const {
			return static_cast<InputField*>(parentWidget());
		}
		friend class InputField;

	};
	friend class Inner;

	void updatePalette();
	void refreshPlaceholder();
	void setErrorShown(bool error);

	void focusInInner(bool focusByMouse);
	void focusOutInner();
	void setFocused(bool focused);

	void processDocumentContentsChange(int position, int charsAdded);

	const style::InputField &_st;

	int _maxLength = -1;
	bool _forcePlaceholderHidden = false;

	object_ptr<Inner> _inner;

	QString _oldtext;

	bool _undoAvailable = false;
	bool _redoAvailable = false;

	bool _customUpDown = true;

	QString _placeholder;
	base::lambda<QString()> _placeholderFactory;
	Animation _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animation _a_borderShown;
	int _borderAnimationStart = 0;
	Animation _a_borderOpacity;
	bool _borderVisible = false;

	Animation _a_focused;
	Animation _a_error;

	bool _focused = false;
	bool _error = false;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;

	bool _correcting = false;
};

class MaskedInputField : public TWidgetHelper<QLineEdit>, private base::Subscriber {
	Q_OBJECT

public:
	MaskedInputField(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

	void showError();

	QRect getTextRect() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(base::lambda<QString()> placeholderFactory);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimations();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	void setText(const QString &text) {
		QLineEdit::setText(text);
		startPlaceholderAnimation();
	}
	void clear() {
		QLineEdit::clear();
		startPlaceholderAnimation();
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
	QString getDisplayedText() const {
		auto result = getLastText();
		if (!_lastPreEditText.isEmpty()) {
			result = result.mid(0, _oldcursor) + _lastPreEditText + result.mid(_oldcursor);
		}
		return result;
	}
	void startBorderAnimation();
	void startPlaceholderAnimation();

	bool event(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	virtual void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) {
	}
	void setCorrectedText(QString &now, int &nowCursor, const QString &newText, int newPos);

	virtual void paintAdditionalPlaceholder(Painter &p, TimeMs ms) {
	}

	style::font phFont() {
		return _st.font;
	}

	void placeholderAdditionalPrepare(Painter &p, TimeMs ms);
	QRect placeholderRect() const;

	void setTextMargins(const QMargins &mrg);
	const style::InputField &_st;

private:
	void updatePalette();
	void refreshPlaceholder();
	void setErrorShown(bool error);

	void setFocused(bool focused);

	int _maxLength = -1;
	bool _forcePlaceholderHidden = false;

	QString _oldtext;
	int _oldcursor = 0;
	QString _lastPreEditText;

	bool _undoAvailable = false;
	bool _redoAvailable = false;

	bool _customUpDown = false;

	QString _placeholder;
	base::lambda<QString()> _placeholderFactory;
	Animation _a_placeholderShifted;
	bool _placeholderShifted = false;
	QPainterPath _placeholderPath;

	Animation _a_borderShown;
	int _borderAnimationStart = 0;
	Animation _a_borderOpacity;
	bool _borderVisible = false;

	Animation _a_focused;
	Animation _a_error;

	bool _focused = false;
	bool _error = false;

	style::margins _textMargins;

	QTimer _touchTimer;
	bool _touchPress = false;
	bool _touchRightButton = false;
	bool _touchMove = false;
	QPoint _touchStart;
};

class CountryCodeInput : public MaskedInputField {
	Q_OBJECT

public:
	CountryCodeInput(QWidget *parent, const style::InputField &st);

public slots:
	void startErasing(QKeyEvent *e);
	void codeSelected(const QString &code);

signals:
	void codeChanged(const QString &code);
	void addedToNumber(const QString &added);

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;

private:
	bool _nosignal;

};

class PhonePartInput : public MaskedInputField {
	Q_OBJECT

public:
	PhonePartInput(QWidget *parent, const style::InputField &st);

public slots:
	void addedToNumber(const QString &added);
	void onChooseCode(const QString &code);

signals:
	void voidBackspace(QKeyEvent *e);

protected:
	void keyPressEvent(QKeyEvent *e) override;

	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

class PasswordInput : public MaskedInputField {
public:
	PasswordInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory = base::lambda<QString()>(), const QString &val = QString());

};

class PortInput : public MaskedInputField {
public:
	PortInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val);

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;

};

class UsernameInput : public MaskedInputField {
public:
	UsernameInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val, bool isLink);

	void setLinkPlaceholder(const QString &placeholder);

protected:
	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:
	PhoneInput(QWidget *parent, const style::InputField &st, base::lambda<QString()> placeholderFactory, const QString &val);

	void clearText();

protected:
	void focusInEvent(QFocusEvent *e) override;

	void correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

} // namespace Ui
