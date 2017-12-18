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
#include "base/flags.h"
#include "base/value_ordering.h"

struct MessageGroupId;
struct HistoryMessageGroup;
struct HistoryMessageReplyMarkup;
class ReplyKeyboard;
class HistoryMessage;
class HistoryMedia;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

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

enum HistoryCursorState {
	HistoryDefaultCursorState,
	HistoryInTextCursorState,
	HistoryInDateCursorState,
	HistoryInForwardedCursorState,
};

struct HistoryTextState {
	HistoryTextState() = default;
	HistoryTextState(not_null<const HistoryItem*> item);
	HistoryTextState(
		not_null<const HistoryItem*> item,
		const Text::StateResult &state);
	HistoryTextState(
		not_null<const HistoryItem*> item,
		ClickHandlerPtr link);
	HistoryTextState(
		std::nullptr_t,
		const Text::StateResult &state)
	: cursor(state.uponSymbol
		? HistoryInTextCursorState
		: HistoryDefaultCursorState)
	, link(state.link)
	, afterSymbol(state.afterSymbol)
	, symbol(state.symbol) {
	}
	HistoryTextState(std::nullptr_t, ClickHandlerPtr link)
	: link(link) {
	}

	FullMsgId itemId;
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

// HistoryMedia has a special owning smart pointer
// which regs/unregs this media to the holding HistoryItem
class HistoryMediaPtr {
public:
	HistoryMediaPtr();
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

class HistoryItem
	: public HistoryElement
	, public RuntimeComposer
	, public ClickHandlerHost {
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
	virtual void applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) {
	}

	UserData *viaBot() const;
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
	void addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content);

	not_null<History*> history() const {
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
	bool isMediaUnread() const;
	void markMediaRead();

	// Zero result means this message is not self-destructing right now.
	virtual TimeMs getSelfDestructIn(TimeMs now) {
		return 0;
	}

	bool definesReplyKeyboard() const;
	MTPDreplyKeyboardMarkup::Flags replyKeyboardFlags() const;

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
	bool indexInUnreadMentions() const {
		return (id > 0);
	}
	bool isSilent() const {
		return _flags & MTPDmessage::Flag::f_silent;
	}
	bool hasOutLayout() const;
	virtual int32 viewsCount() const {
		return hasViews() ? 1 : -1;
	}

	virtual bool needCheck() const {
		return out() || (id < 0 && history()->peer->isSelf());
	}
	virtual bool hasPoint(QPoint point) const {
		return false;
	}

	[[nodiscard]] virtual HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const = 0;
	virtual void updatePressed(QPoint point) {
	}

	[[nodiscard]] virtual TextSelection adjustSelection(
			TextSelection selection,
			TextSelectType type) const {
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

	virtual void addToUnreadMentions(AddToUnreadMentionsMethod method) {
	}
	virtual void eraseFromUnreadMentions() {
	}
	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;

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

	enum class DrawInDialog {
		Normal,
		WithoutSender,
	};

	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	virtual QString inDialogsText(DrawInDialog way) const;
	virtual QString inReplyText() const {
		return notificationText();
	}
	virtual TextWithEntities originalText() const {
		return { QString(), EntitiesInText() };
	}

	virtual void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	}
	virtual ClickHandlerPtr rightActionLink() const {
		return ClickHandlerPtr();
	}
	virtual bool displayRightAction() const {
		return false;
	}
	virtual void drawRightAction(Painter &p, int left, int top, int outerWidth) const {
	}
	virtual void setViewsCount(int32 count) {
	}
	virtual void setId(MsgId newId);

	virtual bool displayEditedBadge() const {
		return false;
	}
	virtual QDateTime displayedEditDate() const {
		return QDateTime();
	}
	virtual void refreshEditedBadge() {
	}

	void drawInDialog(
		Painter &p,
		const QRect &r,
		bool active,
		bool selected,
		DrawInDialog way,
		const HistoryItem *&cacheFor,
		Text &cache) const;

	bool emptyText() const {
		return _text.isEmpty();
	}

	bool isPinned() const;
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
	MsgId replyToId() const;

	PeerData *author() const {
		return isPost() ? history()->peer : from();
	}

	QDateTime dateOriginal() const;
	PeerData *senderOriginal() const;
	PeerData *fromOriginal() const;
	QString authorOriginal() const;
	MsgId idOriginal() const;

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

	int displayedDateHeight() const;
	int marginTop() const;
	int marginBottom() const;
	bool isAttachedToPrevious() const {
		return _flags & MTPDmessage_ClientFlag::f_attach_to_previous;
	}
	bool isAttachedToNext() const {
		return _flags & MTPDmessage_ClientFlag::f_attach_to_next;
	}
	bool displayDate() const;

	bool isInOneDayWithPrevious() const {
		return !isEmpty() && !displayDate();
	}

	bool isEmpty() const;
	bool isHiddenByGroup() const {
		return _flags & MTPDmessage_ClientFlag::f_hidden_by_group;
	}

	MessageGroupId groupId() const;
	bool groupIdValidityChanged();
	void validateGroupId() {
		// Just ignore the result.
		groupIdValidityChanged();
	}
	void makeGroupMember(not_null<HistoryItem*> leader);
	void makeGroupLeader(std::vector<not_null<HistoryItem*>> &&others);
	HistoryMessageGroup *getFullGroup();

	int width() const {
		return _width;
	}

	void clipCallback(Media::Clip::Notification notification);
	void audioTrackUpdated();

	bool computeIsAttachToPrevious(not_null<HistoryItem*> previous);
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
	HistoryItem(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		QDateTime date,
		UserId from);

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

	const not_null<History*> _history;
	not_null<PeerData*> _from;
	HistoryBlock *_block = nullptr;
	int _indexInBlock = -1;
	MTPDmessage::Flags _flags = 0;

	HistoryItem *previousItem() const {
		if (_block && _indexInBlock >= 0) {
			if (_indexInBlock > 0) {
				return _block->items.at(_indexInBlock - 1);
			}
			if (auto previous = _block->previousBlock()) {
				Assert(!previous->items.empty());
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
				Assert(!next->items.empty());
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
	HistoryMessageReplyMarkup *inlineReplyMarkup();
	ReplyKeyboard *inlineReplyKeyboard();
	void invalidateChatsListEntry();

	[[nodiscard]] TextSelection skipTextSelection(
			TextSelection selection) const {
		return internal::unshiftSelection(selection, _text);
	}
	[[nodiscard]] TextSelection unskipTextSelection(
			TextSelection selection) const {
		return internal::shiftSelection(selection, _text);
	}

	Text _text = { int(st::msgMinWidth) };
	int _textWidth = -1;
	int _textHeight = 0;

	HistoryMediaPtr _media;

private:
	void resetGroupMedia(const std::vector<not_null<HistoryItem*>> &others);

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
	static not_null<T*> _create(Args &&... args) {
		auto result = new T(std::forward<Args>(args)...);
		result->finishCreate();
		return result;
	}

};

ClickHandlerPtr goToMessageClickHandler(PeerData *peer, MsgId msgId);

inline ClickHandlerPtr goToMessageClickHandler(HistoryItem *item) {
	return goToMessageClickHandler(item->history()->peer, item->id);
}
