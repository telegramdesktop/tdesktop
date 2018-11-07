/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

class UserData;

namespace Ui {

class PopupMenu;

void InsertEmojiAtCursor(QTextCursor cursor, EmojiPtr emoji);

struct InstantReplaces {
	struct Node {
		QString text;
		std::map<QChar, Node> tail;
	};

	void add(const QString &what, const QString &with);

	static const InstantReplaces &Default();

	int maxLength = 0;
	Node reverseMap;

};

enum class InputSubmitSettings {
	Enter,
	CtrlEnter,
	Both,
	None,
};

class FlatInput : public TWidgetHelper<QLineEdit>, private base::Subscriber {
	Q_OBJECT

public:
	FlatInput(
		QWidget *parent,
		const style::FlatInput &st,
		Fn<QString()> placeholderFactory = nullptr,
		const QString &val = QString());

	void updatePlaceholder();
	void setPlaceholder(Fn<QString()> placeholderFactory);
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
	void submitted(Qt::KeyboardModifiers);
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
	Fn<QString()> _placeholderFactory;

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

class InputField : public RpWidget, private base::Subscriber {
	Q_OBJECT

public:
	enum class Mode {
		SingleLine,
		MultiLine,
	};
	using TagList = TextWithTags::Tags;

	struct MarkdownTag {
		// With each emoji being QChar::ObjectReplacementCharacter.
		int internalStart = 0;
		int internalLength = 0;

		// Adjusted by emoji to match _lastTextWithTags.
		int adjustedStart = 0;
		int adjustedLength = 0;

		bool closed = false;
		QString tag;
	};
	static const QString kTagBold;
	static const QString kTagItalic;
	static const QString kTagCode;
	static const QString kTagPre;

	InputField(
		QWidget *parent,
		const style::InputField &st,
		Fn<QString()> placeholderFactory,
		const QString &value = QString());
	InputField(
		QWidget *parent,
		const style::InputField &st,
		Mode mode,
		Fn<QString()> placeholderFactory,
		const QString &value);
	InputField(
		QWidget *parent,
		const style::InputField &st,
		Mode mode = Mode::SingleLine,
		Fn<QString()> placeholderFactory = nullptr,
		const TextWithTags &value = TextWithTags());

	void showError();

	void setMaxLength(int maxLength);
	void setMinHeight(int minHeight);
	void setMaxHeight(int maxHeight);

	const TextWithTags &getTextWithTags() const {
		return _lastTextWithTags;
	}
	const std::vector<MarkdownTag> &getMarkdownTags() const {
		return _lastMarkdownTags;
	}
	TextWithTags getTextWithTagsPart(int start, int end = -1) const;
	TextWithTags getTextWithAppliedMarkdown() const;
	void insertTag(const QString &text, QString tagId = QString());
	bool empty() const {
		return _lastTextWithTags.text.isEmpty();
	}
	enum class HistoryAction {
		NewEntry,
		MergeEntry,
		Clear,
	};
	void setTextWithTags(
		const TextWithTags &textWithTags,
		HistoryAction historyAction = HistoryAction::NewEntry);

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

	struct EditLinkSelection {
		int from = 0;
		int till = 0;
	};
	enum class EditLinkAction {
		Check,
		Edit,
	};
	void setEditLinkCallback(
		Fn<bool(
			EditLinkSelection selection,
			QString text,
			QString link,
			EditLinkAction action)> callback);

	void setAdditionalMargin(int margin);

	void setInstantReplaces(const InstantReplaces &replaces);
	void setInstantReplacesEnabled(rpl::producer<bool> enabled);
	void setMarkdownReplacesEnabled(rpl::producer<bool> enabled);
	void commitInstantReplacement(
		int from,
		int till,
		const QString &with,
		std::optional<QString> checkOriginal = std::nullopt);
	bool commitMarkdownReplacement(
		int from,
		int till,
		const QString &tag,
		const QString &edge = QString());
	void commitMarkdownLinkEdit(
		EditLinkSelection selection,
		const QString &text,
		const QString &link);
	void toggleSelectionMarkdown(const QString &tag);
	void clearSelectionMarkdown();
	static bool IsValidMarkdownLink(const QString &link);

	const QString &getLastText() const {
		return _lastTextWithTags.text;
	}
	void setPlaceholder(
		Fn<QString()> placeholderFactory,
		int afterSymbols = 0);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimating();
	void setFocusFast() {
		setDisplayFocused(true);
		setFocus();
	}

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	bool hasText() const;
	void selectAll();

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	using SubmitSettings = InputSubmitSettings;
	void setSubmitSettings(SubmitSettings settings);
	static bool ShouldSubmit(
		SubmitSettings settings,
		Qt::KeyboardModifiers modifiers);
	void customUpDown(bool isCustom);
	void customTab(bool isCustom);
	int borderAnimationStart() const;

	not_null<QTextDocument*> document();
	not_null<const QTextDocument*> document() const;
	void setTextCursor(const QTextCursor &cursor);
	void setCursorPosition(int position);
	QTextCursor textCursor() const;
	void setText(const QString &text);
	void clear();
	bool hasFocus() const;
	void setFocus();
	void clearFocus();
	not_null<QTextEdit*> rawTextEdit();
	not_null<const QTextEdit*> rawTextEdit() const;

	enum class MimeAction {
		Check,
		Insert,
	};
	using MimeDataHook = Fn<bool(
		not_null<const QMimeData*> data,
		MimeAction action)>;
	void setMimeDataHook(MimeDataHook hook) {
		_mimeDataHook = std::move(hook);
	}

	const rpl::variable<int> &scrollTop() const;
	int scrollTopMax() const;
	void scrollTo(int top);

	~InputField();

private slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onCursorPositionChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void onFocusInner();

signals:
	void changed();
	void submitted(Qt::KeyboardModifiers);
	void cancelled();
	void tabbed();
	void focused();
	void blurred();
	void resized();

protected:
	void startPlaceholderAnimation();
	void startBorderAnimation();

	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;
	friend class Inner;

	void handleContentsChanged();
	bool viewportEventInner(QEvent *e);
	QVariant loadResource(int type, const QUrl &name);
	void handleTouchEvent(QTouchEvent *e);

	void updatePalette();
	void refreshPlaceholder();
	int placeholderSkipWidth() const;

	bool heightAutoupdated();
	void checkContentHeight();
	void setErrorShown(bool error);

	void focusInEventInner(QFocusEvent *e);
	void focusOutEventInner(QFocusEvent *e);
	void setFocused(bool focused);
	void keyPressEventInner(QKeyEvent *e);
	void contextMenuEventInner(QContextMenuEvent *e);
	void dropEventInner(QDropEvent *e);
	void inputMethodEventInner(QInputMethodEvent *e);

	QMimeData *createMimeDataFromSelectionInner() const;
	bool canInsertFromMimeDataInner(const QMimeData *source) const;
	void insertFromMimeDataInner(const QMimeData *source);
	TextWithTags getTextWithTagsSelected() const;

	// "start" and "end" are in coordinates of text where emoji are replaced
	// by ObjectReplacementCharacter. If "end" = -1 means get text till the end.
	QString getTextPart(
		int start,
		int end,
		TagList &outTagsList,
		bool &outTagsChanged,
		std::vector<MarkdownTag> *outMarkdownTags = nullptr) const;

	// After any characters added we must postprocess them. This includes:
	// 1. Replacing font family to semibold for ~ characters, if we used Open Sans 13px.
	// 2. Replacing font family from semibold for all non-~ characters, if we used ...
	// 3. Replacing emoji code sequences by ObjectReplacementCharacters with emoji pics.
	// 4. Interrupting tags in which the text was inserted by any char except a letter.
	// 5. Applying tags from "_insertedTags" in case we pasted text with tags, not just text.
	// Rule 4 applies only if we inserted chars not in the middle of a tag (but at the end).
	void processFormatting(int changedPosition, int changedEnd);

	void chopByMaxLength(int insertPosition, int insertLength);

	bool processMarkdownReplaces(const QString &appended);
	//bool processMarkdownReplace(const QString &tag);
	void addMarkdownActions(not_null<QMenu*> menu, QContextMenuEvent *e);
	void addMarkdownMenuAction(
		not_null<QMenu*> menu,
		not_null<QAction*> action);
	bool handleMarkdownKey(QKeyEvent *e);

	// We don't want accidentally detach InstantReplaces map.
	// So we access it only by const reference from this method.
	const InstantReplaces &instantReplaces() const;
	void processInstantReplaces(const QString &appended);
	void applyInstantReplace(const QString &what, const QString &with);

	struct EditLinkData {
		int from = 0;
		int till = 0;
		QString link;
	};
	EditLinkData selectionEditLinkData(EditLinkSelection selection) const;
	EditLinkSelection editLinkSelection(QContextMenuEvent *e) const;
	void editMarkdownLink(EditLinkSelection selection);

	bool revertFormatReplace();

	void highlightMarkdown();

	const style::InputField &_st;

	Mode _mode = Mode::SingleLine;
	int _maxLength = -1;
	int _minHeight = -1;
	int _maxHeight = -1;
	bool _forcePlaceholderHidden = false;
	bool _reverseMarkdownReplacement = false;

	const std::unique_ptr<Inner> _inner;

	TextWithTags _lastTextWithTags;
	std::vector<MarkdownTag> _lastMarkdownTags;
	QString _lastPreEditText;
	Fn<bool(
		EditLinkSelection selection,
		QString text,
		QString link,
		EditLinkAction action)> _editLinkCallback;

	// Tags list which we should apply while setText() call or insert from mime data.
	TagList _insertedTags;
	bool _insertedTagsAreFromMime;

	// Override insert position and charsAdded from complex text editing
	// (like drag-n-drop in the same text edit field).
	int _realInsertPosition = -1;
	int _realCharsAdded = 0;

	std::unique_ptr<TagMimeProcessor> _tagMimeProcessor;

	SubmitSettings _submitSettings = SubmitSettings::Enter;
	bool _markdownEnabled = false;
	bool _undoAvailable = false;
	bool _redoAvailable = false;
	bool _inDrop = false;
	bool _inHeightCheck = false;
	int _additionalMargin = 0;

	bool _customUpDown = false;
	bool _customTab = false;

	QString _placeholder;
	Fn<QString()> _placeholderFactory;
	int _placeholderAfterSymbols = 0;
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
	MimeDataHook _mimeDataHook;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

	QTextCharFormat _defaultCharFormat;

	rpl::variable<int> _scrollTop;

	InstantReplaces _mutableInstantReplaces;
	bool _instantReplacesEnabled = true;

};

class MaskedInputField
	: public RpWidgetWrap<QLineEdit>
	, private base::Subscriber {
	Q_OBJECT

	using Parent = RpWidgetWrap<QLineEdit>;
public:
	MaskedInputField(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory = Fn<QString()>(), const QString &val = QString());

	void showError();

	QRect getTextRect() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	int borderAnimationStart() const;

	const QString &getLastText() const {
		return _oldtext;
	}
	void setPlaceholder(Fn<QString()> placeholderFactory);
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void setDisplayFocused(bool focused);
	void finishAnimating();
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
	void submitted(Qt::KeyboardModifiers);
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

	bool eventHook(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;

	virtual void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
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
	Fn<QString()> _placeholderFactory;
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
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

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

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

class PasswordInput : public MaskedInputField {
public:
	PasswordInput(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory = Fn<QString()>(), const QString &val = QString());

};

class PortInput : public MaskedInputField {
public:
	PortInput(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory, const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

class HexInput : public MaskedInputField {
public:
	HexInput(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory, const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

class UsernameInput : public MaskedInputField {
public:
	UsernameInput(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory, const QString &val, bool isLink);

	void setLinkPlaceholder(const QString &placeholder);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:
	PhoneInput(QWidget *parent, const style::InputField &st, Fn<QString()> placeholderFactory, const QString &val);

	void clearText();

protected:
	void focusInEvent(QFocusEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p, TimeMs ms) override;

private:
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

} // namespace Ui
