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

#include "base/runtime_composer.h"

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace style {
struct BotKeyboardButton;
struct RippleAnimation;
} // namespace style

class HistoryElement {
public:
	HistoryElement() = default;
	HistoryElement(const HistoryElement &other) = delete;
	HistoryElement &operator=(const HistoryElement &other) = delete;

	int maxWidth() const {
		return _maxw;
	}
	int minHeight() const {
		return _minh;
	}
	int height() const {
		return _height;
	}

	virtual ~HistoryElement() = default;

protected:
	mutable int _maxw = 0;
	mutable int _minh = 0;
	mutable int _height = 0;

};

class HistoryMessage;

enum HistoryCursorState {
	HistoryDefaultCursorState,
	HistoryInTextCursorState,
	HistoryInDateCursorState,
	HistoryInForwardedCursorState,
};

struct HistoryTextState {
	HistoryTextState() = default;
	HistoryTextState(const Text::StateResult &state)
		: cursor(state.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState)
		, link(state.link)
		, afterSymbol(state.afterSymbol)
		, symbol(state.symbol) {
	}
	HistoryTextState &operator=(const Text::StateResult &state) {
		cursor = state.uponSymbol ? HistoryInTextCursorState : HistoryDefaultCursorState;
		link = state.link;
		afterSymbol = state.afterSymbol;
		symbol = state.symbol;
		return *this;
	}
	HistoryCursorState cursor = HistoryDefaultCursorState;
	ClickHandlerPtr link;
	bool afterSymbol = false;
	uint16 symbol = 0;
};

struct HistoryStateRequest {
	Text::StateRequest::Flags flags = Text::StateRequest::Flag::LookupLink;
	Text::StateRequest forText() const {
		Text::StateRequest result;
		result.flags = flags;
		return result;
	}
};

enum InfoDisplayType {
	InfoDisplayDefault,
	InfoDisplayOverImage,
	InfoDisplayOverBackground,
};

struct HistoryMessageVia : public RuntimeComponent<HistoryMessageVia> {
	void create(int32 userId);
	void resize(int32 availw) const;

	UserData *_bot = nullptr;
	mutable QString _text;
	mutable int _width = 0;
	mutable int _maxWidth = 0;
	ClickHandlerPtr _lnk;
};

struct HistoryMessageViews : public RuntimeComponent<HistoryMessageViews> {
	QString _viewsText;
	int _views = 0;
	int _viewsWidth = 0;
};

struct HistoryMessageSigned : public RuntimeComponent<HistoryMessageSigned> {
	void create(const QString &author, const QString &date);
	int maxWidth() const;

	QString _author;
	Text _signature;
};

struct HistoryMessageEdited : public RuntimeComponent<HistoryMessageEdited> {
	void create(const QDateTime &editDate, const QString &date);
	int maxWidth() const;

	QDateTime _editDate;
	Text _edited;
};

struct HistoryMessageForwarded : public RuntimeComponent<HistoryMessageForwarded> {
	void create(const HistoryMessageVia *via) const;

	QDateTime _originalDate;
	PeerData *_originalSender = nullptr;
	QString _originalAuthor;
	MsgId _originalId = 0;
	mutable Text _text = { 1 };
};

struct HistoryMessageReply : public RuntimeComponent<HistoryMessageReply> {
	HistoryMessageReply &operator=(HistoryMessageReply &&other) {
		replyToMsgId = other.replyToMsgId;
		std::swap(replyToMsg, other.replyToMsg);
		replyToLnk = std::move(other.replyToLnk);
		replyToName = std::move(other.replyToName);
		replyToText = std::move(other.replyToText);
		replyToVersion = other.replyToVersion;
		_maxReplyWidth = other._maxReplyWidth;
		_replyToVia = std::move(other._replyToVia);
		return *this;
	}
	~HistoryMessageReply() {
		// clearData() should be called by holder
		Expects(replyToMsg == nullptr);
		Expects(_replyToVia == nullptr);
	}

	bool updateData(HistoryMessage *holder, bool force = false);
	void clearData(HistoryMessage *holder); // must be called before destructor

	bool isNameUpdated() const;
	void updateName() const;
	void resize(int width) const;
	void itemRemoved(HistoryMessage *holder, HistoryItem *removed);

	enum PaintFlag {
		PaintInBubble = 0x01,
		PaintSelected = 0x02,
	};
	Q_DECLARE_FLAGS(PaintFlags, PaintFlag);
	void paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const;

	MsgId replyToId() const {
		return replyToMsgId;
	}
	int replyToWidth() const {
		return _maxReplyWidth;
	}
	ClickHandlerPtr replyToLink() const {
		return replyToLnk;
	}

	MsgId replyToMsgId = 0;
	HistoryItem *replyToMsg = nullptr;
	ClickHandlerPtr replyToLnk;
	mutable Text replyToName, replyToText;
	mutable int replyToVersion = 0;
	mutable int _maxReplyWidth = 0;
	std::unique_ptr<HistoryMessageVia> _replyToVia;
	int toWidth = 0;
};
Q_DECLARE_OPERATORS_FOR_FLAGS(HistoryMessageReply::PaintFlags);

class ReplyKeyboard;
struct HistoryMessageReplyMarkup : public RuntimeComponent<HistoryMessageReplyMarkup> {
	HistoryMessageReplyMarkup() = default;
	HistoryMessageReplyMarkup(MTPDreplyKeyboardMarkup::Flags f) : flags(f) {
	}

	void create(const MTPReplyMarkup &markup);
	void create(const HistoryMessageReplyMarkup &markup);

	struct Button {
		enum class Type {
			Default,
			Url,
			Callback,
			RequestPhone,
			RequestLocation,
			SwitchInline,
			SwitchInlineSame,
			Game,
			Buy,
		};
		Type type;
		QString text;
		QByteArray data;
		mutable mtpRequestId requestId;
	};
	using ButtonRow = QVector<Button>;
	using ButtonRows = QVector<ButtonRow>;

	ButtonRows rows;
	MTPDreplyKeyboardMarkup::Flags flags = 0;

	std::unique_ptr<ReplyKeyboard> inlineKeyboard;

	// If >= 0 it holds the y coord of the inlineKeyboard before the last edition.
	int oldTop = -1;

private:
	void createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v);

};

class ReplyMarkupClickHandler : public LeftButtonClickHandler {
public:
	ReplyMarkupClickHandler(const HistoryItem *item, int row, int col);

	QString tooltip() const override {
		return _fullDisplayed ? QString() : buttonText();
	}

	void setFullDisplayed(bool full) {
		_fullDisplayed = full;
	}

	// Copy to clipboard support.
	void copyToClipboard() const override;
	QString copyToClipboardContextItemText() const override;

	// Finds the corresponding button in the items markup struct.
	// If the button is not found it returns nullptr.
	// Note: it is possible that we will point to the different button
	// than the one was used when constructing the handler, but not a big deal.
	const HistoryMessageReplyMarkup::Button *getButton() const;

	// We hold only FullMsgId, not HistoryItem*, because all click handlers
	// are activated async and the item may be already destroyed.
	void setMessageId(const FullMsgId &msgId) {
		_itemId = msgId;
	}

protected:
	void onClickImpl() const override;

private:
	FullMsgId _itemId;
	int _row, _col;
	bool _fullDisplayed = true;

	// Returns the full text of the corresponding button.
	QString buttonText() const;

};

class ReplyKeyboard {
private:
	struct Button;

public:
	class Style {
	public:
		Style(const style::BotKeyboardButton &st) : _st(&st) {
		}

		virtual void startPaint(Painter &p) const = 0;
		virtual const style::TextStyle &textStyle() const = 0;

		int buttonSkip() const;
		int buttonPadding() const;
		int buttonHeight() const;
		virtual int buttonRadius() const = 0;

		virtual void repaint(gsl::not_null<const HistoryItem*> item) const = 0;
		virtual ~Style() {
		}

	protected:
		virtual void paintButtonBg(Painter &p, const QRect &rect, float64 howMuchOver) const = 0;
		virtual void paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const = 0;
		virtual void paintButtonLoading(Painter &p, const QRect &rect) const = 0;
		virtual int minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const = 0;

	private:
		const style::BotKeyboardButton *_st;

		void paintButton(Painter &p, int outerWidth, const ReplyKeyboard::Button &button, TimeMs ms) const;
		friend class ReplyKeyboard;

	};
	typedef std::unique_ptr<Style> StylePtr;

	ReplyKeyboard(const HistoryItem *item, StylePtr &&s);
	ReplyKeyboard(const ReplyKeyboard &other) = delete;
	ReplyKeyboard &operator=(const ReplyKeyboard &other) = delete;

	bool isEnoughSpace(int width, const style::BotKeyboardButton &st) const;
	void setStyle(StylePtr &&s);
	void resize(int width, int height);

	// what width and height will best fit this keyboard
	int naturalWidth() const;
	int naturalHeight() const;

	void paint(Painter &p, int outerWidth, const QRect &clip, TimeMs ms) const;
	ClickHandlerPtr getState(QPoint point) const;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active);
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed);

	void clearSelection();
	void updateMessageId();

private:
	void startAnimation(int i, int j, int direction);

	friend class Style;
	using ReplyMarkupClickHandlerPtr = QSharedPointer<ReplyMarkupClickHandler>;
	struct Button {
		Text text = { 1 };
		QRect rect;
		int characters = 0;
		float64 howMuchOver = 0.;
		HistoryMessageReplyMarkup::Button::Type type;
		ReplyMarkupClickHandlerPtr link;
		mutable QSharedPointer<Ui::RippleAnimation> ripple;
	};
	using ButtonRow = QVector<Button>;
	using ButtonRows = QVector<ButtonRow>;

	struct ButtonCoords {
		int i, j;
	};
	ButtonCoords findButtonCoordsByClickHandler(const ClickHandlerPtr &p);

	using Animations = QMap<int, TimeMs>;
	void step_selected(TimeMs ms, bool timer);

	const HistoryItem *_item;
	int _width = 0;

	ButtonRows _rows;

	Animations _animations;
	BasicAnimation _a_selected;

	StylePtr _st;

	ClickHandlerPtr _savedPressed;
	ClickHandlerPtr _savedActive;
	mutable QPoint _savedCoords;

};

// Any HistoryItem can have this Component for
// displaying the day mark above the message.
struct HistoryMessageDate : public RuntimeComponent<HistoryMessageDate> {
	void init(const QDateTime &date);

	int height() const;
	void paint(Painter &p, int y, int w) const;

	QString _text;
	int _width = 0;
};

// Any HistoryItem can have this Component for
// displaying the unread messages bar above the message.
struct HistoryMessageUnreadBar : public RuntimeComponent<HistoryMessageUnreadBar> {
	void init(int count);

	static int height();
	static int marginTop();

	void paint(Painter &p, int y, int w) const;

	QString _text;
	int _width = 0;

	// If unread bar is freezed the new messages do not
	// increment the counter displayed by this bar.
	//
	// It happens when we've opened the conversation and
	// we've seen the bar and new messages are marked as read
	// as soon as they are added to the chat history.
	bool _freezed = false;

};

class HistoryWebPage;

// Special type of Component for the channel actions log.
struct HistoryMessageLogEntryOriginal : public RuntimeComponent<HistoryMessageLogEntryOriginal> {
	HistoryMessageLogEntryOriginal();
	HistoryMessageLogEntryOriginal(HistoryMessageLogEntryOriginal &&other);
	HistoryMessageLogEntryOriginal &operator=(HistoryMessageLogEntryOriginal &&other);
	~HistoryMessageLogEntryOriginal();

	std::unique_ptr<HistoryWebPage> _page;

};

// HistoryMedia has a special owning smart pointer
// which regs/unregs this media to the holding HistoryItem
class HistoryMedia;
class HistoryMediaPtr {
public:
	HistoryMediaPtr() = default;
	HistoryMediaPtr(const HistoryMediaPtr &other) = delete;
	HistoryMediaPtr &operator=(const HistoryMediaPtr &other) = delete;
	HistoryMediaPtr(std::unique_ptr<HistoryMedia> other);
	HistoryMediaPtr &operator=(std::unique_ptr<HistoryMedia> other);

	HistoryMedia *get() const {
		return _pointer.get();
	}
	void reset(std::unique_ptr<HistoryMedia> pointer = nullptr);
	bool isNull() const {
		return !_pointer;
	}

	HistoryMedia *operator->() const {
		return get();
	}
	HistoryMedia &operator*() const {
		Expects(!isNull());
		return *get();
	}
	explicit operator bool() const {
		return !isNull();
	}
	~HistoryMediaPtr();

private:
	std::unique_ptr<HistoryMedia> _pointer;

};

namespace internal {

TextSelection unshiftSelection(TextSelection selection, uint16 byLength);
TextSelection shiftSelection(TextSelection selection, uint16 byLength);
inline TextSelection unshiftSelection(TextSelection selection, const Text &byText) {
	return ::internal::unshiftSelection(selection, byText.length());
}
inline TextSelection shiftSelection(TextSelection selection, const Text &byText) {
	return ::internal::shiftSelection(selection, byText.length());
}

} // namespace internal

class HistoryItem : public HistoryElement, public RuntimeComposer, public ClickHandlerHost {
public:
	int resizeGetHeight(int newWidth) {
		if (_flags & MTPDmessage_ClientFlag::f_pending_init_dimensions) {
			_flags &= ~MTPDmessage_ClientFlag::f_pending_init_dimensions;
			initDimensions();
		}
		if (_flags & MTPDmessage_ClientFlag::f_pending_resize) {
			_flags &= ~MTPDmessage_ClientFlag::f_pending_resize;
		}
		_width = newWidth;
		return resizeContentGetHeight();
	}
	virtual void draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms) const = 0;

	virtual void dependencyItemRemoved(HistoryItem *dependency) {
	}
	virtual bool updateDependencyItem() {
		return true;
	}
	virtual MsgId dependencyMsgId() const {
		return 0;
	}
	virtual bool notificationReady() const {
		return true;
	}

	UserData *viaBot() const {
		if (auto via = Get<HistoryMessageVia>()) {
			return via->_bot;
		}
		return nullptr;
	}
	UserData *getMessageBot() const {
		if (auto bot = viaBot()) {
			return bot;
		}
		auto bot = from()->asUser();
		if (!bot) {
			bot = history()->peer->asUser();
		}
		return (bot && bot->botInfo) ? bot : nullptr;
	};

	bool isLogEntry() const {
		return (id > ServerMaxMsgId);
	}
	void addLogEntryOriginal(WebPageId localId, const QString &label, const TextWithEntities &content);

	History *history() const {
		return _history;
	}
	PeerData *from() const {
		return _from;
	}
	HistoryBlock *block() {
		return _block;
	}
	const HistoryBlock *block() const {
		return _block;
	}
	void destroy();
	void detach();
	void detachFast();
	bool detached() const {
		return !_block;
	}
	void attachToBlock(HistoryBlock *block, int index) {
		Expects(!isLogEntry());
		Expects(_block == nullptr);
		Expects(_indexInBlock < 0);
		Expects(block != nullptr);
		Expects(index >= 0);

		_block = block;
		_indexInBlock = index;
		setPendingResize();
	}
	void setIndexInBlock(int index) {
		Expects(_block != nullptr);
		Expects(index >= 0);

		_indexInBlock = index;
	}
	int indexInBlock() const {
		Expects((_indexInBlock >= 0) == (_block != nullptr));
		Expects((_block == nullptr) || (_block->items[_indexInBlock] == this));

		return _indexInBlock;
	}
	bool out() const {
		return _flags & MTPDmessage::Flag::f_out;
	}
	bool unread() const;
	bool mentionsMe() const {
		return _flags & MTPDmessage::Flag::f_mentioned;
	}
	bool isMediaUnread() const {
		return (_flags & MTPDmessage::Flag::f_media_unread) && (channelId() == NoChannel);
	}
	void markMediaRead() {
		_flags &= ~MTPDmessage::Flag::f_media_unread;
		markMediaAsReadHook();
	}

	// Zero result means this message is not self-destructing right now.
	virtual TimeMs getSelfDestructIn(TimeMs now) {
		return 0;
	}

	bool definesReplyKeyboard() const {
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
				return false;
			}
			return true;
		}

		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		return (_flags & MTPDmessage::Flag::f_reply_markup);
	}
	MTPDreplyKeyboardMarkup::Flags replyKeyboardFlags() const {
		Expects(definesReplyKeyboard());
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			return markup->flags;
		}

		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		return qFlags(MTPDreplyKeyboardMarkup_ClientFlag::f_zero);
	}
	bool hasSwitchInlineButton() const {
		return _flags & MTPDmessage_ClientFlag::f_has_switch_inline_button;
	}
	bool hasTextLinks() const {
		return _flags & MTPDmessage_ClientFlag::f_has_text_links;
	}
	bool isGroupMigrate() const {
		return _flags & MTPDmessage_ClientFlag::f_is_group_migrate;
	}
	bool hasViews() const {
		return _flags & MTPDmessage::Flag::f_views;
	}
	bool isPost() const {
		return _flags & MTPDmessage::Flag::f_post;
	}
	bool indexInOverview() const {
		return (id > 0) && (!history()->isChannel() || history()->isMegagroup() || isPost());
	}
	bool isSilent() const {
		return _flags & MTPDmessage::Flag::f_silent;
	}
	bool hasOutLayout() const {
		return out() && !isPost();
	}
	virtual int32 viewsCount() const {
		return hasViews() ? 1 : -1;
	}

	virtual bool needCheck() const {
		return out() || (id < 0 && history()->peer->isSelf());
	}
	virtual bool hasPoint(QPoint point) const {
		return false;
	}

	virtual HistoryTextState getState(QPoint point, HistoryStateRequest request) const WARN_UNUSED_RESULT = 0;
	virtual void updatePressed(QPoint point) {
	}

	virtual TextSelection adjustSelection(TextSelection selection, TextSelectType type) const WARN_UNUSED_RESULT {
		return selection;
	}

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	virtual bool serviceMsg() const {
		return false;
	}
	virtual void applyEdition(const MTPDmessage &message) {
	}
	virtual void applyEdition(const MTPDmessageService &message) {
	}
	virtual void updateMedia(const MTPMessageMedia *media) {
	}
	virtual void updateReplyMarkup(const MTPReplyMarkup *markup) {
	}
	virtual int32 addToOverview(AddToOverviewMethod method) {
		return 0;
	}
	virtual void eraseFromOverview() {
	}
	virtual bool hasBubble() const {
		return false;
	}

	void previousItemChanged();
	void nextItemChanged();

	virtual TextWithEntities selectedText(TextSelection selection) const {
		return { qsl("[-]"), EntitiesInText() };
	}

	virtual QString notificationHeader() const {
		return QString();
	}
	virtual QString notificationText() const;

	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	virtual QString inDialogsText() const;
	virtual QString inReplyText() const {
		return notificationText();
	}
	virtual TextWithEntities originalText() const {
		return { QString(), EntitiesInText() };
	}

	virtual void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	}
	virtual ClickHandlerPtr fastShareLink() const {
		return ClickHandlerPtr();
	}
	virtual bool displayFastShare() const {
		return false;
	}
	virtual void drawFastShare(Painter &p, int left, int top, int outerWidth) const {
	}
	virtual void setViewsCount(int32 count) {
	}
	virtual void setId(MsgId newId);
	void drawInDialog(Painter &p, const QRect &r, bool active, bool selected, const HistoryItem *&cacheFor, Text &cache) const;

	bool emptyText() const {
		return _text.isEmpty();
	}

	bool canPin() const;
	bool canForward() const;
	bool canEdit(const QDateTime &cur) const;
	bool canDelete() const;
	bool canDeleteForEveryone(const QDateTime &cur) const;
	bool suggestBanReport() const;
	bool suggestDeleteAllReport() const;

	bool hasDirectLink() const;
	QString directLink() const;

	int y() const {
		return _y;
	}
	void setY(int y) {
		_y = y;
	}
	MsgId id;
	QDateTime date;

	ChannelId channelId() const {
		return _history->channelId();
	}
	FullMsgId fullId() const {
		return FullMsgId(channelId(), id);
	}

	HistoryMedia *getMedia() const {
		return _media.get();
	}
	virtual void setText(const TextWithEntities &textWithEntities) {
	}
	virtual bool textHasLinks() const {
		return false;
	}

	virtual int infoWidth() const {
		return 0;
	}
	virtual int timeLeft() const {
		return 0;
	}
	virtual int timeWidth() const {
		return 0;
	}
	virtual bool pointInTime(int right, int bottom, QPoint point, InfoDisplayType type) const {
		return false;
	}

	int skipBlockWidth() const {
		return st::msgDateSpace + infoWidth() - st::msgDateDelta.x();
	}
	int skipBlockHeight() const {
		return st::msgDateFont->height - st::msgDateDelta.y();
	}
	QString skipBlock() const {
		return textcmdSkipBlock(skipBlockWidth(), skipBlockHeight());
	}

	virtual HistoryMessage *toHistoryMessage() { // dynamic_cast optimize
		return nullptr;
	}
	virtual const HistoryMessage *toHistoryMessage() const { // dynamic_cast optimize
		return nullptr;
	}
	MsgId replyToId() const {
		if (auto reply = Get<HistoryMessageReply>()) {
			return reply->replyToId();
		}
		return 0;
	}

	bool hasFromName() const {
		return (!out() || isPost()) && !history()->peer->isUser();
	}
	PeerData *author() const {
		return isPost() ? history()->peer : from();
	}

	QDateTime dateOriginal() const {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->_originalDate;
		}
		return date;
	}
	PeerData *senderOriginal() const {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->_originalSender;
		}
		auto peer = history()->peer;
		return (peer->isChannel() && !peer->isMegagroup()) ? peer : from();
	}
	PeerData *fromOriginal() const {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			if (auto user = forwarded->_originalSender->asUser()) {
				return user;
			}
		}
		return from();
	}
	QString authorOriginal() const {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->_originalAuthor;
		} else if (auto msgsigned = Get<HistoryMessageSigned>()) {
			return msgsigned->_author;
		}
		return QString();
	}
	MsgId idOriginal() const {
		if (auto forwarded = Get<HistoryMessageForwarded>()) {
			return forwarded->_originalId;
		}
		return id;
	}

	// count > 0 - creates the unread bar if necessary and
	// sets unread messages count if bar is not freezed yet
	// count <= 0 - destroys the unread bar
	void setUnreadBarCount(int count);
	void destroyUnreadBar();

	// marks the unread bar as freezed so that unread
	// messages count will not change for this bar
	// when the new messages arrive in this chat history
	void setUnreadBarFreezed();

	bool pendingResize() const {
		return _flags & MTPDmessage_ClientFlag::f_pending_resize;
	}
	void setPendingResize() {
		_flags |= MTPDmessage_ClientFlag::f_pending_resize;
		if (!detached() || isLogEntry()) {
			_history->setHasPendingResizedItems();
		}
	}
	bool pendingInitDimensions() const {
		return _flags & MTPDmessage_ClientFlag::f_pending_init_dimensions;
	}
	void setPendingInitDimensions() {
		_flags |= MTPDmessage_ClientFlag::f_pending_init_dimensions;
		setPendingResize();
	}

	int displayedDateHeight() const {
		if (auto date = Get<HistoryMessageDate>()) {
			return date->height();
		}
		return 0;
	}
	int marginTop() const {
		int result = 0;
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
		result += displayedDateHeight();
		if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
			result += unreadbar->height();
		}
		return result;
	}
	int marginBottom() const {
		return st::msgMargin.bottom();
	}
	bool isAttachedToPrevious() const {
		return _flags & MTPDmessage_ClientFlag::f_attach_to_previous;
	}
	bool isAttachedToNext() const {
		return _flags & MTPDmessage_ClientFlag::f_attach_to_next;
	}
	bool displayDate() const {
		return Has<HistoryMessageDate>();
	}

	bool isInOneDayWithPrevious() const {
		return !isEmpty() && !displayDate();
	}

	bool isEmpty() const {
		return _text.isEmpty() && !_media && !Has<HistoryMessageLogEntryOriginal>();
	}

	int width() const {
		return _width;
	}

	void clipCallback(Media::Clip::Notification notification);
	void audioTrackUpdated();

	bool computeIsAttachToPrevious(gsl::not_null<HistoryItem*> previous);
	void setLogEntryDisplayDate(bool displayDate) {
		Expects(isLogEntry());
		setDisplayDate(displayDate);
	}
	void setLogEntryAttachToPrevious(bool attachToPrevious) {
		Expects(isLogEntry());
		setAttachToPrevious(attachToPrevious);
	}
	void setLogEntryAttachToNext(bool attachToNext) {
		Expects(isLogEntry());
		setAttachToNext(attachToNext);
	}

	~HistoryItem();

protected:
	HistoryItem(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime msgDate, int32 from);

	// To completely create history item we need to call
	// a virtual method, it can not be done from constructor.
	virtual void finishCreate();

	// Called from resizeGetHeight() when MTPDmessage_ClientFlag::f_pending_init_dimensions is set.
	virtual void initDimensions() = 0;

	virtual void markMediaAsReadHook() {
	}

	virtual int resizeContentGetHeight() = 0;

	void finishEdition(int oldKeyboardTop);
	void finishEditionToEmpty();

	gsl::not_null<History*> _history;
	gsl::not_null<PeerData*> _from;
	HistoryBlock *_block = nullptr;
	int _indexInBlock = -1;
	MTPDmessage::Flags _flags = 0;

	mutable int32 _authorNameVersion = 0;

	HistoryItem *previousItem() const {
		if (_block && _indexInBlock >= 0) {
			if (_indexInBlock > 0) {
				return _block->items.at(_indexInBlock - 1);
			}
			if (auto previous = _block->previousBlock()) {
				t_assert(!previous->items.isEmpty());
				return previous->items.back();
			}
		}
		return nullptr;
	}
	HistoryItem *nextItem() const {
		if (_block && _indexInBlock >= 0) {
			if (_indexInBlock + 1 < _block->items.size()) {
				return _block->items.at(_indexInBlock + 1);
			}
			if (auto next = _block->nextBlock()) {
				t_assert(!next->items.isEmpty());
				return next->items.front();
			}
		}
		return nullptr;
	}

	// This should be called only from previousItemChanged()
	// to add required bits to the Composer mask
	// after that always use Has<HistoryMessageDate>().
	void recountDisplayDate();

	// This should be called only from previousItemChanged() or when
	// HistoryMessageDate or HistoryMessageUnreadBar bit is changed in the Composer mask
	// then the result should be cached in a client side flag MTPDmessage_ClientFlag::f_attach_to_previous.
	void recountAttachToPrevious();

	// This should be called only from recountDisplayDate().
	// Also this is called from setLogEntryDisplayDate() for channel log entries.
	void setDisplayDate(bool displayDate);

	// This should be called only from recountAttachToPrevious().
	// Also this is called from setLogEntryAttachToPrevious() for channel log entries.
	void setAttachToPrevious(bool attachToNext);

	// This should be called only from recountAttachToPrevious() of the next item
	// or when the next item is removed through nextItemChanged() call.
	// Also this is called from setLogEntryAttachToNext() for channel log entries.
	void setAttachToNext(bool attachToNext);

	const HistoryMessageReplyMarkup *inlineReplyMarkup() const {
		return const_cast<HistoryItem*>(this)->inlineReplyMarkup();
	}
	const ReplyKeyboard *inlineReplyKeyboard() const {
		return const_cast<HistoryItem*>(this)->inlineReplyKeyboard();
	}
	HistoryMessageReplyMarkup *inlineReplyMarkup() {
		if (auto markup = Get<HistoryMessageReplyMarkup>()) {
			if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
				return markup;
			}
		}
		return nullptr;
	}
	ReplyKeyboard *inlineReplyKeyboard() {
		if (auto markup = inlineReplyMarkup()) {
			return markup->inlineKeyboard.get();
		}
		return nullptr;
	}

	TextSelection skipTextSelection(TextSelection selection) const WARN_UNUSED_RESULT {
		return internal::unshiftSelection(selection, _text);
	}
	TextSelection unskipTextSelection(TextSelection selection) const WARN_UNUSED_RESULT {
		return internal::shiftSelection(selection, _text);
	}

	Text _text = { int(st::msgMinWidth) };
	int _textWidth = -1;
	int _textHeight = 0;

	HistoryMediaPtr _media;

private:
	int _y = 0;
	int _width = 0;

};

// make all the constructors in HistoryItem children protected
// and wrapped with a static create() call with the same args
// so that history item can not be created directly, without
// calling a virtual finishCreate() method
template <typename T>
class HistoryItemInstantiated {
public:
	template <typename ...Args>
	static gsl::not_null<T*> _create(Args &&... args) {
		auto result = new T(std::forward<Args>(args)...);
		result->finishCreate();
		return result;
	}

};

ClickHandlerPtr goToMessageClickHandler(PeerData *peer, MsgId msgId);

inline ClickHandlerPtr goToMessageClickHandler(HistoryItem *item) {
	return goToMessageClickHandler(item->history()->peer, item->id);
}
