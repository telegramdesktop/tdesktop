/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"
#include "base/flags.h"
#include "data/data_media_types.h"
#include "history/history_item_edition.h"
#include "history/history_item_reply_markup.h"

#include <any>

class HiddenSenderInfo;
class History;
struct HistoryMessageReply;
struct HistoryMessageViews;
struct HistoryMessageMarkupData;
struct HistoryMessageReplyMarkup;
struct HistoryMessageTranslation;
struct HistoryServiceDependentData;
enum class HistorySelfDestructType;
struct PreparedServiceText;
class ReplyKeyboard;
struct LanguageId;

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
struct RecentReaction;
struct ReactionId;
class Media;
struct MessageReaction;
class MessageReactions;
class ForumTopic;
class Thread;
struct SponsoredFrom;
class Story;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryUnreadThings {
enum class AddType;
} // namespace HistoryUnreadThings

namespace HistoryView {
struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
enum class Context : char;
class ElementDelegate;
class Element;
class Message;
class Service;
class ServiceMessagePainter;
} // namespace HistoryView

class HistoryItem final : public RuntimeComposer<HistoryItem> {
public:
	[[nodiscard]] static std::unique_ptr<Data::Media> CreateMedia(
		not_null<HistoryItem*> item,
		const MTPMessageMedia &media);

	HistoryItem(
		not_null<History*> history,
		MsgId id,
		const MTPDmessage &data,
		MessageFlags localFlags);
	HistoryItem(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data,
		MessageFlags localFlags);
	HistoryItem(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageEmpty &data,
		MessageFlags localFlags);

	HistoryItem( // Sponsored message.
		not_null<History*> history,
		MsgId id,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities,
		HistoryItem *injectedAfter);

	HistoryItem( // Local message.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia &media,
		HistoryMessageMarkupData &&markup,
		uint64 groupedId);
	HistoryItem( // Local service message.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PreparedServiceText &&message,
		PeerId from = 0,
		PhotoData *photo = nullptr);
	HistoryItem( // Local forwarded.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<HistoryItem*> original,
		MsgId topicRootId);
	HistoryItem( // Local photo.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup);
	HistoryItem( // Local document.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup);
	HistoryItem( // Local game.
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		HistoryMessageMarkupData &&markup);
	HistoryItem(not_null<History*> history, not_null<Data::Story*> story);
	~HistoryItem();

	struct Destroyer {
		void operator()(HistoryItem *value);
	};

	void dependencyItemRemoved(not_null<HistoryItem*> dependency);
	void dependencyStoryRemoved(not_null<Data::Story*> dependency);
	void updateDependencyItem();
	[[nodiscard]] MsgId dependencyMsgId() const;
	[[nodiscard]] bool notificationReady() const;
	[[nodiscard]] PeerData *specialNotificationPeer() const;
	void checkStoryForwardInfo();
	void checkBuyButton();

	void updateServiceText(PreparedServiceText &&text);
	void updateStoryMentionText();

	[[nodiscard]] UserData *viaBot() const;
	[[nodiscard]] UserData *getMessageBot() const;
	[[nodiscard]] bool isHistoryEntry() const;
	[[nodiscard]] bool isAdminLogEntry() const;
	[[nodiscard]] bool isFromScheduled() const;
	[[nodiscard]] bool isScheduled() const;
	[[nodiscard]] bool isSponsored() const;
	[[nodiscard]] bool skipNotification() const;
	[[nodiscard]] bool isUserpicSuggestion() const;

	void addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content);

	[[nodiscard]] not_null<Data::Thread*> notificationThread() const;
	[[nodiscard]] not_null<History*> history() const {
		return _history;
	}
	[[nodiscard]] Data::ForumTopic *topic() const;
	[[nodiscard]] not_null<PeerData*> from() const {
		return _from;
	}
	[[nodiscard]] HistoryView::Element *mainView() const {
		return _mainView;
	}
	void setMainView(not_null<HistoryView::Element*> view) {
		_mainView = view;
	}
	void refreshMainView();
	void clearMainView();
	void removeMainView();

	void invalidateChatListEntry();

	void destroy();
	[[nodiscard]] bool out() const {
		return _flags & MessageFlag::Outgoing;
	}
	[[nodiscard]] bool isPinned() const {
		return _flags & MessageFlag::Pinned;
	}
	[[nodiscard]] bool unread(not_null<Data::Thread*> thread) const;
	[[nodiscard]] bool showNotification() const;
	void markClientSideAsRead();
	[[nodiscard]] bool mentionsMe() const;
	[[nodiscard]] bool isUnreadMention() const;
	[[nodiscard]] bool hasUnreadReaction() const;
	[[nodiscard]] bool isUnreadMedia() const;
	[[nodiscard]] bool isIncomingUnreadMedia() const;
	[[nodiscard]] bool hasUnreadMediaFlag() const;
	void markReactionsRead();
	void markMediaAndMentionRead();
	bool markContentsRead(bool fromThisClient = false);
	void setIsPinned(bool isPinned);

	// For edit media in history_message.
	void returnSavedMedia();
	void savePreviousMedia();
	[[nodiscard]] bool isEditingMedia() const;
	void clearSavedMedia();

	// Zero result means this message is not self-destructing right now.
	[[nodiscard]] crl::time getSelfDestructIn(crl::time now);

	[[nodiscard]] bool definesReplyKeyboard() const;
	[[nodiscard]] ReplyMarkupFlags replyKeyboardFlags() const;

	void cacheOnlyEmojiAndSpaces(bool only);
	[[nodiscard]] bool isOnlyEmojiAndSpaces() const;
	[[nodiscard]] bool hasSwitchInlineButton() const {
		return _flags & MessageFlag::HasSwitchInlineButton;
	}
	[[nodiscard]] bool hasTextLinks() const {
		return _flags & MessageFlag::HasTextLinks;
	}
	[[nodiscard]] bool isGroupEssential() const {
		return _flags & MessageFlag::IsGroupEssential;
	}
	[[nodiscard]] bool isLocalUpdateMedia() const {
		return _flags & MessageFlag::IsLocalUpdateMedia;
	}
	void setIsLocalUpdateMedia(bool flag) {
		if (flag) {
			_flags |= MessageFlag::IsLocalUpdateMedia;
		} else {
			_flags &= ~MessageFlag::IsLocalUpdateMedia;
		}
	}
	[[nodiscard]] bool isGroupMigrate() const {
		return isGroupEssential() && isEmpty();
	}
	[[nodiscard]] bool hasViews() const {
		return _flags & MessageFlag::HasViews;
	}
	[[nodiscard]] bool isPost() const {
		return _flags & MessageFlag::Post;
	}
	[[nodiscard]] bool isSilent() const {
		return _flags & MessageFlag::Silent;
	}
	[[nodiscard]] bool isSending() const {
		return _flags & MessageFlag::BeingSent;
	}
	[[nodiscard]] bool hasFailed() const {
		return _flags & MessageFlag::SendingFailed;
	}
	[[nodiscard]] bool hideEditedBadge() const {
		return (_flags & MessageFlag::HideEdited);
	}
	[[nodiscard]] bool isLocal() const {
		return _flags & MessageFlag::Local;
	}
	[[nodiscard]] bool isFakeBotAbout() const {
		return _flags & MessageFlag::FakeBotAbout;
	}
	[[nodiscard]] bool isRegular() const;
	[[nodiscard]] bool isUploading() const;
	void sendFailed();
	[[nodiscard]] int viewsCount() const;
	[[nodiscard]] int repliesCount() const;
	[[nodiscard]] bool repliesAreComments() const;
	[[nodiscard]] bool externalReply() const;
	[[nodiscard]] bool hasExtendedMediaPreview() const;

	void setCommentsInboxReadTill(MsgId readTillId);
	void setCommentsMaxId(MsgId maxId);
	void setCommentsPossibleMaxId(MsgId possibleMaxId);
	[[nodiscard]] bool areCommentsUnread() const;

	[[nodiscard]] FullMsgId commentsItemId() const;
	void setCommentsItemId(FullMsgId id);

	[[nodiscard]] bool needCheck() const;

	[[nodiscard]] bool isService() const;
	void applyEdition(HistoryMessageEdition &&edition);
	void applyChanges(not_null<Data::Story*> story);

	void applyEdition(const MTPDmessageService &message);
	void applyEdition(const MTPMessageExtendedMedia &media);
	void updateForwardedInfo(const MTPMessageFwdHeader *fwd);
	void updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media);
	void applySentMessage(const MTPDmessage &data);
	void applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready);
	void updateReactions(const MTPMessageReactions *reactions);

	void applyEditionToHistoryCleared();
	void updateReplyMarkup(HistoryMessageMarkupData &&markup);
	void contributeToSlowmode(TimeId realDate = 0);

	void addToUnreadThings(HistoryUnreadThings::AddType type);
	void destroyHistoryEntry();
	[[nodiscard]] Storage::SharedMediaTypesMask sharedMediaTypes() const;

	void indexAsNewItem();
	void addToSharedMediaIndex();
	void removeFromSharedMediaIndex();

	struct NotificationTextOptions {
		bool spoilerLoginCode = false;
	};
	[[nodiscard]] QString notificationHeader() const;
	[[nodiscard]] TextWithEntities notificationText(
		NotificationTextOptions options) const;
	[[nodiscard]] TextWithEntities notificationText() const {
		return notificationText({});
	}

	using ToPreviewOptions = HistoryView::ToPreviewOptions;
	using ItemPreview = HistoryView::ItemPreview;

	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	[[nodiscard]] ItemPreview toPreview(ToPreviewOptions options) const;
	[[nodiscard]] TextWithEntities inReplyText() const;
	[[nodiscard]] const TextWithEntities &originalText() const;
	[[nodiscard]] const TextWithEntities &translatedText() const;
	[[nodiscard]] TextWithEntities translatedTextWithLocalEntities() const;
	[[nodiscard]] const std::vector<ClickHandlerPtr> &customTextLinks() const;
	[[nodiscard]] TextForMimeData clipboardText() const;

	bool changeViewsCount(int count);
	void setForwardsCount(int count);
	void setReplies(HistoryMessageRepliesData &&data);
	void clearReplies();
	void changeRepliesCount(int delta, PeerId replier);
	void setReplyFields(
		MsgId replyTo,
		MsgId replyToTop,
		bool isForumPost);
	void setPostAuthor(const QString &author);
	void setRealId(MsgId newId);
	void incrementReplyToTopCounter();

	[[nodiscard]] bool emptyText() const {
		return _text.empty();
	}

	[[nodiscard]] bool canPin() const;
	[[nodiscard]] bool canBeEdited() const;
	[[nodiscard]] bool canStopPoll() const;
	[[nodiscard]] bool forbidsForward() const;
	[[nodiscard]] bool forbidsSaving() const;
	[[nodiscard]] bool allowsSendNow() const;
	[[nodiscard]] bool allowsForward() const;
	[[nodiscard]] bool allowsEdit(TimeId now) const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canDeleteForEveryone(TimeId now) const;
	[[nodiscard]] bool suggestReport() const;
	[[nodiscard]] bool suggestBanReport() const;
	[[nodiscard]] bool suggestDeleteAllReport() const;
	[[nodiscard]] ChatRestriction requiredSendRight() const;
	[[nodiscard]] bool requiresSendInlineRight() const;
	[[nodiscard]] std::optional<QString> errorTextForForward(
		not_null<Data::Thread*> to) const;
	[[nodiscard]] const HistoryMessageTranslation *translation() const;
	[[nodiscard]] bool translationShowRequiresCheck(LanguageId to) const;
	bool translationShowRequiresRequest(LanguageId to);
	void translationDone(LanguageId to, TextWithEntities result);

	[[nodiscard]] bool canReact() const;
	enum class ReactionSource {
		Selector,
		Quick,
		Existing,
	};
	void toggleReaction(
		const Data::ReactionId &reaction,
		ReactionSource source);
	void updateReactionsUnknown();
	[[nodiscard]] auto reactions() const
		-> const std::vector<Data::MessageReaction> &;
	[[nodiscard]] auto recentReactions() const
		-> const base::flat_map<
			Data::ReactionId,
			std::vector<Data::RecentReaction>> &;
	[[nodiscard]] bool canViewReactions() const;
	[[nodiscard]] std::vector<Data::ReactionId> chosenReactions() const;
	[[nodiscard]] Data::ReactionId lookupUnreadReaction(
		not_null<UserData*> from) const;
	[[nodiscard]] crl::time lastReactionsRefreshTime() const;

	[[nodiscard]] bool hasDirectLink() const;
	[[nodiscard]] bool changesWallPaper() const;

	[[nodiscard]] FullMsgId fullId() const;
	[[nodiscard]] GlobalMsgId globalId() const;
	[[nodiscard]] Data::MessagePosition position() const;
	[[nodiscard]] TimeId date() const;

	[[nodiscard]] static TimeId NewMessageDate(TimeId scheduled);

	[[nodiscard]] Data::Media *media() const {
		return _media.get();
	}
	[[nodiscard]] bool computeDropForwardedInfo() const;
	void setText(const TextWithEntities &textWithEntities);

	[[nodiscard]] MsgId replyToId() const;
	[[nodiscard]] MsgId replyToTop() const;
	[[nodiscard]] MsgId topicRootId() const;
	[[nodiscard]] FullStoryId replyToStory() const;
	[[nodiscard]] FullReplyTo replyTo() const;
	[[nodiscard]] bool inThread(MsgId rootId) const;

	[[nodiscard]] not_null<PeerData*> author() const;

	[[nodiscard]] TimeId dateOriginal() const;
	[[nodiscard]] PeerData *senderOriginal() const;
	[[nodiscard]] const HiddenSenderInfo *hiddenSenderInfo() const;
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

	[[nodiscard]] std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr);

	void updateDate(TimeId newDate);
	[[nodiscard]] bool canUpdateDate() const;
	void customEmojiRepaint();

	[[nodiscard]] TimeId ttlDestroyAt() const {
		return _ttlDestroyAt;
	}

	MsgId id;

private:
	struct CreateConfig;

	struct SavedMediaData {
		TextWithEntities text;
		std::unique_ptr<Data::Media> media;
	};

	HistoryItem(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PeerId from);

	void createComponentsHelper(
		MessageFlags flags,
		FullReplyTo replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup);
	void createComponents(CreateConfig &&config);
	void setupForwardedComponent(const CreateConfig &config);

	[[nodiscard]] bool generateLocalEntitiesByReply() const;
	[[nodiscard]] TextWithEntities withLocalEntities(
		const TextWithEntities &textWithEntities) const;
	void setTextValue(TextWithEntities text);
	[[nodiscard]] bool isTooOldForEdit(TimeId now) const;
	[[nodiscard]] bool isLegacyMessage() const {
		return _flags & MessageFlag::Legacy;
	}

	[[nodiscard]] bool checkCommentsLinkedChat(ChannelId id) const;

	void setReplyMarkup(HistoryMessageMarkupData &&markup);

	void changeReplyToTopCounter(
		not_null<HistoryMessageReply*> reply,
		int delta);
	void refreshRepliesText(
		not_null<HistoryMessageViews*> views,
		bool forceResize = false);

	[[nodiscard]] bool checkRepliesPts(
		const HistoryMessageRepliesData &data) const;

	[[nodiscard]] HistoryServiceDependentData *GetServiceDependentData();
	[[nodiscard]] auto GetServiceDependentData() const
		-> const HistoryServiceDependentData *;
	void updateDependentServiceText();
	bool updateServiceDependent(bool force = false);
	void setServiceText(PreparedServiceText &&prepared);

	void setStoryFields(not_null<Data::Story*> story);
	void finishEdition(int oldKeyboardTop);
	void finishEditionToEmpty();

	void clearDependencyMessage();
	void setupChatThemeChange();
	void setupTTLChange();

	void translationToggle(
		not_null<HistoryMessageTranslation*> translation,
		bool used);
	void setSelfDestruct(HistorySelfDestructType type, MTPint mtpTTLvalue);

	TextWithEntities fromLinkText() const;
	ClickHandlerPtr fromLink() const;

	void setGroupId(MessageGroupId groupId);

	static void FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data);
	void createComponents(const MTPDmessage &data);
	void setMedia(const MTPMessageMedia &media);
	void applyServiceDateEdition(const MTPDmessageService &data);
	void setReactions(const MTPMessageReactions *reactions);
	[[nodiscard]] bool changeReactions(const MTPMessageReactions *reactions);
	void setServiceMessageByAction(const MTPmessageAction &action);
	void applyAction(const MTPMessageAction &action);
	void refreshMedia(const MTPMessageMedia *media);
	void refreshSentMedia(const MTPMessageMedia *media);
	void createServiceFromMtp(const MTPDmessage &message);
	void createServiceFromMtp(const MTPDmessageService &message);
	void applyTTL(const MTPDmessage &data);
	void applyTTL(const MTPDmessageService &data);

	void applyTTL(TimeId destroyAt);

	// For an invoice button we replace the button text with a "Receipt" key.
	// It should show the receipt for the payed invoice. Still let mobile apps do that.
	void replaceBuyWithReceiptInMarkup();

	void setSponsoredFrom(const Data::SponsoredFrom &from);

	[[nodiscard]] PreparedServiceText preparePinnedText();
	[[nodiscard]] PreparedServiceText prepareGameScoreText();
	[[nodiscard]] PreparedServiceText preparePaymentSentText();
	[[nodiscard]] PreparedServiceText prepareStoryMentionText();
	[[nodiscard]] PreparedServiceText prepareInvitedToCallText(
		const std::vector<not_null<UserData*>> &users,
		CallId linkCallId);
	[[nodiscard]] PreparedServiceText prepareCallScheduledText(
		TimeId scheduleDate);

	const not_null<History*> _history;
	const not_null<PeerData*> _from;
	MessageFlags _flags = 0;

	TextWithEntities _text;

	std::unique_ptr<SavedMediaData> _savedLocalEditMediaData;
	std::unique_ptr<Data::Media> _media;
	std::unique_ptr<Data::MessageReactions> _reactions;
	crl::time _reactionsLastRefreshed = 0;

	TimeId _date = 0;
	TimeId _ttlDestroyAt = 0;

	HistoryView::Element *_mainView = nullptr;
	MessageGroupId _groupId = MessageGroupId();

	friend class HistoryView::Element;
	friend class HistoryView::Message;
	friend class HistoryView::Service;
	friend class HistoryView::ServiceMessagePainter;

};
