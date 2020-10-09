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
#include "data/data_media_types.h"

enum class UnreadMentionType;
struct HistoryMessageReplyMarkup;
class ReplyKeyboard;
class HistoryMessage;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : signed char;
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
class Media;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {
struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
enum class Context : char;
class ElementDelegate;
} // namespace HistoryView

struct HiddenSenderInfo;
class History;

class HistoryItem : public RuntimeComposer<HistoryItem> {
public:
	static not_null<HistoryItem*> Create(
		not_null<History*> history,
		const MTPMessage &message,
		MTPDmessage_ClientFlags clientFlags);

	struct Destroyer {
		void operator()(HistoryItem *value);
	};

	virtual void dependencyItemRemoved(HistoryItem *dependency) {
	}
	virtual bool updateDependencyItem() {
		return true;
	}
	virtual MsgId dependencyMsgId() const {
		return 0;
	}
	[[nodiscard]] virtual bool notificationReady() const {
		return true;
	}
	[[nodiscard]] PeerData *specialNotificationPeer() const;

	[[nodiscard]] UserData *viaBot() const;
	[[nodiscard]] UserData *getMessageBot() const;
	[[nodiscard]] bool isHistoryEntry() const;
	[[nodiscard]] bool isAdminLogEntry() const;
	[[nodiscard]] bool isFromScheduled() const;
	[[nodiscard]] bool isScheduled() const;

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
	void setMainView(not_null<HistoryView::Element*> view) {
		_mainView = view;
	}
	void refreshMainView();
	void clearMainView();
	void removeMainView();

	void destroy();
	[[nodiscard]] bool out() const {
		return _flags & MTPDmessage::Flag::f_out;
	}
	[[nodiscard]] bool isPinned() const {
		return _flags & MTPDmessage::Flag::f_pinned;
	}
	[[nodiscard]] bool unread() const;
	[[nodiscard]] bool showNotification() const;
	void markClientSideAsRead();
	[[nodiscard]] bool mentionsMe() const;
	[[nodiscard]] bool isUnreadMention() const;
	[[nodiscard]] bool isUnreadMedia() const;
	[[nodiscard]] bool hasUnreadMediaFlag() const;
	void markMediaRead();
	void setIsPinned(bool isPinned);

	// For edit media in history_message.
	virtual void returnSavedMedia() {};
	void savePreviousMedia() {
		_savedLocalEditMediaData = {
			originalText(),
			_media->clone(this),
		};
	}
	[[nodiscard]] bool isEditingMedia() const {
		return _savedLocalEditMediaData.media != nullptr;
	}
	void clearSavedMedia() {
		_savedLocalEditMediaData = {};
	}

	// Zero result means this message is not self-destructing right now.
	virtual crl::time getSelfDestructIn(crl::time now) {
		return 0;
	}

	[[nodiscard]] bool definesReplyKeyboard() const;
	[[nodiscard]] MTPDreplyKeyboardMarkup::Flags replyKeyboardFlags() const;

	[[nodiscard]] bool hasSwitchInlineButton() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_has_switch_inline_button;
	}
	[[nodiscard]] bool hasTextLinks() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_has_text_links;
	}
	[[nodiscard]] bool isGroupEssential() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_is_group_essential;
	}
	[[nodiscard]] bool isLocalUpdateMedia() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_is_local_update_media;
	}
	void setIsLocalUpdateMedia(bool flag) {
		if (flag) {
			_clientFlags |= MTPDmessage_ClientFlag::f_is_local_update_media;
		} else {
			_clientFlags &= ~MTPDmessage_ClientFlag::f_is_local_update_media;
		}
	}
	[[nodiscard]] bool isGroupMigrate() const {
		return isGroupEssential() && isEmpty();
	}
	[[nodiscard]] bool isIsolatedEmoji() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_isolated_emoji;
	}
	[[nodiscard]] bool hasViews() const {
		return _flags & MTPDmessage::Flag::f_views;
	}
	[[nodiscard]] bool isPost() const {
		return _flags & MTPDmessage::Flag::f_post;
	}
	[[nodiscard]] bool isSilent() const {
		return _flags & MTPDmessage::Flag::f_silent;
	}
	[[nodiscard]] bool isSending() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_sending;
	}
	[[nodiscard]] bool hasFailed() const {
		return _clientFlags & MTPDmessage_ClientFlag::f_failed;
	}
	void sendFailed();
	[[nodiscard]] virtual int viewsCount() const {
		return hasViews() ? 1 : -1;
	}
	[[nodiscard]] virtual int repliesCount() const {
		return 0;
	}
	[[nodiscard]] virtual bool repliesAreComments() const {
		return false;
	}
	[[nodiscard]] virtual bool externalReply() const {
		return false;
	}

	[[nodiscard]] virtual MsgId repliesInboxReadTill() const {
		return MsgId(0);
	}
	virtual void setRepliesInboxReadTill(MsgId readTillId) {
	}
	[[nodiscard]] virtual MsgId computeRepliesInboxReadTillFull() const {
		return MsgId(0);
	}
	[[nodiscard]] virtual MsgId repliesOutboxReadTill() const {
		return MsgId(0);
	}
	virtual void setRepliesOutboxReadTill(MsgId readTillId) {
	}
	[[nodiscard]] virtual MsgId computeRepliesOutboxReadTillFull() const {
		return MsgId(0);
	}
	virtual void setRepliesMaxId(MsgId maxId) {
	}
	virtual void setRepliesPossibleMaxId(MsgId possibleMaxId) {
	}
	[[nodiscard]] virtual bool areRepliesUnread() const {
		return false;
	}

	[[nodiscard]] virtual FullMsgId commentsItemId() const {
		return FullMsgId();
	}
	virtual void setCommentsItemId(FullMsgId id) {
	}

	[[nodiscard]] virtual bool needCheck() const;

	[[nodiscard]] virtual bool serviceMsg() const {
		return false;
	}
	virtual void applyEdition(const MTPDmessage &message) {
	}
	virtual void applyEdition(const MTPDmessageService &message) {
	}
	void applyEditionToHistoryCleared();
	virtual void updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) {
	}
	virtual void updateReplyMarkup(const MTPReplyMarkup *markup) {
	}
	virtual void updateForwardedInfo(const MTPMessageFwdHeader *fwd) {
	}
	virtual void contributeToSlowmode(TimeId realDate = 0) {
	}

	virtual void addToUnreadMentions(UnreadMentionType type);
	virtual void destroyHistoryEntry() {
	}
	[[nodiscard]] virtual Storage::SharedMediaTypesMask sharedMediaTypes() const = 0;

	void indexAsNewItem();

	[[nodiscard]] virtual QString notificationHeader() const {
		return QString();
	}
	[[nodiscard]] virtual QString notificationText() const;

	enum class DrawInDialog {
		Normal,
		WithoutSender,
	};

	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	[[nodiscard]] virtual QString inDialogsText(DrawInDialog way) const;
	[[nodiscard]] virtual QString inReplyText() const {
		return inDialogsText(DrawInDialog::WithoutSender);
	}
	[[nodiscard]] virtual Ui::Text::IsolatedEmoji isolatedEmoji() const;
	[[nodiscard]] virtual TextWithEntities originalText() const {
		return TextWithEntities();
	}
	[[nodiscard]] virtual TextForMimeData clipboardText() const {
		return TextForMimeData();
	}

	virtual void setViewsCount(int count) {
	}
	virtual void setForwardsCount(int count) {
	}
	virtual void setReplies(const MTPMessageReplies &data) {
	}
	virtual void clearReplies() {
	}
	virtual void changeRepliesCount(int delta, PeerId replier) {
	}
	virtual void setReplyToTop(MsgId replyToTop) {
	}
	virtual void setPostAuthor(const QString &author) {
	}
	virtual void setRealId(MsgId newId);
	virtual void incrementReplyToTopCounter() {
	}

	void drawInDialog(
		Painter &p,
		const QRect &r,
		bool active,
		bool selected,
		DrawInDialog way,
		const HistoryItem *&cacheFor,
		Ui::Text::String &cache) const;

	[[nodiscard]] bool emptyText() const {
		return _text.isEmpty();
	}

	[[nodiscard]] bool canPin() const;
	[[nodiscard]] bool canStopPoll() const;
	[[nodiscard]] virtual bool allowsSendNow() const;
	[[nodiscard]] virtual bool allowsForward() const;
	[[nodiscard]] virtual bool allowsEdit(TimeId now) const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canDeleteForEveryone(TimeId now) const;
	[[nodiscard]] bool suggestReport() const;
	[[nodiscard]] bool suggestBanReport() const;
	[[nodiscard]] bool suggestDeleteAllReport() const;

	[[nodiscard]] bool hasDirectLink() const;

	[[nodiscard]] ChannelId channelId() const;
	[[nodiscard]] FullMsgId fullId() const {
		return FullMsgId(channelId(), id);
	}
	[[nodiscard]] Data::MessagePosition position() const;
	[[nodiscard]] TimeId date() const;

	[[nodiscard]] static TimeId NewMessageDate(TimeId scheduled);

	[[nodiscard]] Data::Media *media() const {
		return _media.get();
	}
	virtual void setText(const TextWithEntities &textWithEntities) {
	}
	[[nodiscard]] virtual bool textHasLinks() const {
		return false;
	}

	[[nodiscard]] virtual HistoryMessage *toHistoryMessage() { // dynamic_cast optimize
		return nullptr;
	}
	[[nodiscard]] virtual const HistoryMessage *toHistoryMessage() const { // dynamic_cast optimize
		return nullptr;
	}
	[[nodiscard]] MsgId replyToId() const;
	[[nodiscard]] MsgId replyToTop() const;

	[[nodiscard]] not_null<PeerData*> author() const;

	[[nodiscard]] TimeId dateOriginal() const;
	[[nodiscard]] PeerData *senderOriginal() const;
	[[nodiscard]] const HiddenSenderInfo *hiddenForwardedInfo() const;
	[[nodiscard]] not_null<PeerData*> fromOriginal() const;
	[[nodiscard]] QString authorOriginal() const;
	[[nodiscard]] MsgId idOriginal() const;

	[[nodiscard]] bool isEmpty() const;

	[[nodiscard]] MessageGroupId groupId() const;

	[[nodiscard]] const HistoryMessageReplyMarkup *inlineReplyMarkup() const {
		return const_cast<HistoryItem*>(this)->inlineReplyMarkup();
	}
	[[nodiscard]] const ReplyKeyboard *inlineReplyKeyboard() const {
		return const_cast<HistoryItem*>(this)->inlineReplyKeyboard();
	}
	[[nodiscard]] HistoryMessageReplyMarkup *inlineReplyMarkup();
	[[nodiscard]] ReplyKeyboard *inlineReplyKeyboard();

	[[nodiscard]] ChannelData *discussionPostOriginalSender() const;
	[[nodiscard]] bool isDiscussionPost() const;
	[[nodiscard]] HistoryItem *lookupDiscussionPostOriginal() const;
	[[nodiscard]] PeerData *displayFrom() const;

	[[nodiscard]] virtual std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr) = 0;

	void updateDate(TimeId newDate);
	[[nodiscard]] bool canUpdateDate() const;

	[[nodiscard]] bool canBeEditedFromHistory() const;

	virtual ~HistoryItem();

	MsgId id;

protected:
	HistoryItem(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		TimeId date,
		PeerId from);

	virtual void markMediaAsReadHook() {
	}

	void finishEdition(int oldKeyboardTop);
	void finishEditionToEmpty();

	const not_null<History*> _history;
	not_null<PeerData*> _from;
	MTPDmessage::Flags _flags = 0;
	MTPDmessage_ClientFlags _clientFlags = 0;

	void invalidateChatListEntry();

	void setGroupId(MessageGroupId groupId);

	Ui::Text::String _text = { st::msgMinWidth };
	int _textWidth = -1;
	int _textHeight = 0;

	struct SavedMediaData {
		TextWithEntities text;
		std::unique_ptr<Data::Media> media;
	};

	SavedMediaData _savedLocalEditMediaData;
	std::unique_ptr<Data::Media> _media;

private:
	TimeId _date = 0;

	HistoryView::Element *_mainView = nullptr;
	friend class HistoryView::Element;

	MessageGroupId _groupId = MessageGroupId();

};

QDateTime ItemDateTime(not_null<const HistoryItem*> item);
QString ItemDateText(not_null<const HistoryItem*> item, bool isUntilOnline);
bool IsItemScheduledUntilOnline(not_null<const HistoryItem*> item);

ClickHandlerPtr goToMessageClickHandler(
	not_null<PeerData*> peer,
	MsgId msgId,
	FullMsgId returnToId = FullMsgId());
ClickHandlerPtr goToMessageClickHandler(
	not_null<HistoryItem*> item,
	FullMsgId returnToId = FullMsgId());
