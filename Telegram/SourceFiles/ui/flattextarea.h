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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "animation.h"

class UserData;
static UserData * const LookingUpInlineBot = SharedMemoryLocation<UserData, 0>();

class FlatTextarea : public QTextEdit {
	Q_OBJECT
	T_WIDGET

public:

	struct Tag {
		int offset, length;
		QString id;
	};
	using TagList = QVector<Tag>;
	struct TextWithTags {
		using Tags = FlatTextarea::TagList;
		QString text;
		Tags tags;
	};

	static QByteArray serializeTagsList(const TagList &tags);
	static TagList deserializeTagsList(QByteArray data, int textLength);
	static QString tagsMimeType();

	FlatTextarea(QWidget *parent, const style::flatTextarea &st, const QString &ph = QString(), const QString &val = QString(), const TagList &tags = TagList());

	void setMaxLength(int32 maxLength);
	void setMinHeight(int32 minHeight);
	void setMaxHeight(int32 maxHeight);

	void setPlaceholder(const QString &ph, int32 afterSymbols = 0);
	void updatePlaceholder();
	void finishPlaceholder();

	QRect getTextRect() const;
	int32 fakeMargin() const;

	void step_appearance(float64 ms, bool timer);

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
	void setTagMimeProcessor(std_::unique_ptr<TagMimeProcessor> &&processor);

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
	void enterEventHook(QEvent *e) {
		return QTextEdit::enterEvent(e);
	}
	void leaveEventHook(QEvent *e) {
		return QTextEdit::leaveEvent(e);
	}

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

	int _minHeight = -1; // < 0 - no autosize
	int _maxHeight = -1;
	int _maxLength = -1;
	SubmitSettings _submitSettings = SubmitSettings::Enter;

	QString _ph, _phelided;
	int _phAfter = 0;
	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor;
	Animation _a_appearance;

	TextWithTags _lastTextWithTags;

	// Tags list which we should apply while setText() call or insert from mime data.
	TagList _insertedTags;
	bool _insertedTagsAreFromMime;

	// Override insert position and charsAdded from complex text editing
	// (like drag-n-drop in the same text edit field).
	int _realInsertPosition = -1;
	int _realCharsAdded = 0;

	std_::unique_ptr<TagMimeProcessor> _tagMimeProcessor;

	style::flatTextarea _st;

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

inline bool operator==(const FlatTextarea::Tag &a, const FlatTextarea::Tag &b) {
	return (a.offset == b.offset) && (a.length == b.length) && (a.id == b.id);
}
inline bool operator!=(const FlatTextarea::Tag &a, const FlatTextarea::Tag &b) {
	return !(a == b);
}

inline bool operator==(const FlatTextarea::TextWithTags &a, const FlatTextarea::TextWithTags &b) {
	return (a.text == b.text) && (a.tags == b.tags);
}
inline bool operator!=(const FlatTextarea::TextWithTags &a, const FlatTextarea::TextWithTags &b) {
	return !(a == b);
}

inline bool operator==(const FlatTextarea::LinkRange &a, const FlatTextarea::LinkRange &b) {
	return (a.start == b.start) && (a.length == b.length);
}
inline bool operator!=(const FlatTextarea::LinkRange &a, const FlatTextarea::LinkRange &b) {
	return !(a == b);
}
