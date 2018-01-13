/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"
#include "base/flags.h"
#include "base/value_ordering.h"
#include "history/history_media_pointer.h"
#include "history/view/history_view_cursor_state.h"

enum class UnreadMentionType;
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

namespace Data {
struct MessagePosition;
} // namespace Data

namespace Window {
class Controller;
} // namespace Window

namespace HistoryView {
enum class Context : char;
} // namespace HistoryView

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

class HistoryItem : public RuntimeComposer {
public:
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
	UserData *getMessageBot() const;

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
	not_null<PeerData*> from() const {
		return _from;
	}
	HistoryView::Element *mainView() const {
		return _mainView;
	}
	void setMainView(HistoryView::Element *view) {
		_mainView = view;
	}
	void clearMainView();
	void removeMainView();

	void destroy();
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
	bool isSilent() const {
		return _flags & MTPDmessage::Flag::f_silent;
	}
	bool hasOutLayout() const;
	virtual int32 viewsCount() const {
		return hasViews() ? 1 : -1;
	}

	virtual bool needCheck() const;

	[[nodiscard]] virtual TextSelection adjustSelection(
			TextSelection selection,
			TextSelectType type) const {
		return selection;
	}

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

	virtual void addToUnreadMentions(UnreadMentionType type);
	virtual void eraseFromUnreadMentions() {
	}
	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;
	void indexAsNewItem();

	virtual bool hasBubble() const {
		return false;
	}

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
	virtual void setRealId(MsgId newId);

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

	MsgId id;
	QDateTime date;

	ChannelId channelId() const;
	FullMsgId fullId() const {
		return FullMsgId(channelId(), id);
	}
	Data::MessagePosition position() const;

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

	not_null<PeerData*> author() const;

	QDateTime dateOriginal() const;
	not_null<PeerData*> senderOriginal() const;
	not_null<PeerData*> fromOriginal() const;
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

	int displayedDateHeight() const;
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

	void clipCallback(Media::Clip::Notification notification);
	void audioTrackUpdated();

	bool isUnderCursor() const;

	HistoryItem *previousItem() const;
	HistoryItem *nextItem() const;

	virtual std::unique_ptr<HistoryView::Element> createView(
		not_null<Window::Controller*> controller,
		HistoryView::Context context) = 0;

	virtual ~HistoryItem();

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

	virtual void markMediaAsReadHook() {
	}

	void finishEdition(int oldKeyboardTop);
	void finishEditionToEmpty();

	const not_null<History*> _history;
	not_null<PeerData*> _from;
	MTPDmessage::Flags _flags = 0;

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

	HistoryView::Element *_mainView = nullptr;
	friend class HistoryView::Element;

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

ClickHandlerPtr goToMessageClickHandler(
	not_null<PeerData*> peer,
	MsgId msgId);
ClickHandlerPtr goToMessageClickHandler(not_null<HistoryItem*> item);
