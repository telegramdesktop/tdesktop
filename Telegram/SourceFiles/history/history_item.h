/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"
#include "data/data_media_types.h"
#include "history/history_item_edition.h"

#include <any>

class HiddenSenderInfo;
class History;

struct HistoryMessageReply;
struct HistoryMessageViews;
struct HistoryMessageMarkupData;
struct HistoryMessageReplyMarkup;
struct HistoryMessageTranslation;
struct HistoryMessageForwarded;
struct HistoryMessageSuggestedPost;
struct HistoryServiceDependentData;
struct HistoryServiceTodoCompletions;
enum class HistorySelfDestructType;
struct PreparedServiceText;
struct MessageFactcheck;
class ReplyKeyboard;
struct LanguageId;
enum class SuggestionActions : uchar;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : signed char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

namespace Data {
struct MessagePosition;
struct RecentReaction;
struct MessageReactionsTopPaid;
struct ReactionId;
class Media;
struct MessageReaction;
class MessageReactions;
class ForumTopic;
class Thread;
struct SponsoredFrom;
class Story;
class SavedSublist;
struct PaidReactionSend;
struct SendError;
} // namespace Data

namespace HistoryUnreadThings {
enum class AddType;
} // namespace HistoryUnreadThings

namespace HistoryView {
class ElementDelegate;
class Element;
class Message;
class Service;
class ServiceMessagePainter;
} // namespace HistoryView

struct HistoryItemCommonFields {
	MsgId id = 0;
	MessageFlags flags = 0;
	PeerId from = 0;
	FullReplyTo replyTo;
	TimeId date = 0;
	BusinessShortcutId shortcutId = 0;
	int starsPaid = 0;
	UserId viaBotId = 0;
	QString postAuthor;
	uint64 groupedId = 0;
	EffectId effectId = 0;
	HistoryMessageMarkupData markup;
	HistoryMessageSuggestInfo suggest;
	bool ignoreForwardFrom = false;
	bool ignoreForwardCaptions = false;
};

enum class HistoryReactionSource : char {
	Selector,
	Quick,
	Existing,
};

enum class PaidPostType : uchar {
	None,
	Stars,
	Ton,
};

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
	HistoryItem( // Story wrap.
		not_null<History*> history,
		MsgId id,
		not_null<Data::Story*> story);

	HistoryItem( // Local message.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia &media);
	HistoryItem( // Local service message.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		PreparedServiceText &&message,
		PhotoData *photo = nullptr);
	HistoryItem( // Local forwarded.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		not_null<HistoryItem*> original);
	HistoryItem( // Local photo.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption);
	HistoryItem( // Local document.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		not_null<DocumentData*> document,
		const TextWithEntities &caption);
	HistoryItem( // Local game.
		not_null<History*> history,
		HistoryItemCommonFields &&fields,
		not_null<GameData*> game);
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

	void resolveDependent();

	void updateServiceText(PreparedServiceText &&text);
	void updateStoryMentionText();

	[[nodiscard]] UserData *viaBot() const;
	[[nodiscard]] UserData *getMessageBot() const;
	[[nodiscard]] bool isHistoryEntry() const;
	[[nodiscard]] bool isAdminLogEntry() const;
	[[nodiscard]] bool isFromScheduled() const;
	[[nodiscard]] bool isScheduled() const;
	[[nodiscard]] bool isSponsored() const;
	[[nodiscard]] bool canLookupMessageAuthor() const;
	[[nodiscard]] bool skipNotification() const;
	[[nodiscard]] bool isUserpicSuggestion() const;
	[[nodiscard]] BusinessShortcutId shortcutId() const;
	[[nodiscard]] bool isBusinessShortcut() const;
	void setRealShortcutId(BusinessShortcutId id);
	void setCustomServiceLink(ClickHandlerPtr link);

	void addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content);
	void setFactcheck(MessageFactcheck info);
	[[nodiscard]] bool hasUnrequestedFactcheck() const;
	[[nodiscard]] TextWithEntities factcheckText() const;

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
	[[nodiscard]] bool invertMedia() const {
		return _flags & MessageFlag::InvertMedia;
	}
	[[nodiscard]] bool storyInProfile() const {
		return _flags & MessageFlag::StoryInProfile;
	}
	[[nodiscard]] bool unread(not_null<Data::Thread*> thread) const;
	[[nodiscard]] bool showNotification() const;
	void markClientSideAsRead();
	[[nodiscard]] bool mentionsMe() const;
	[[nodiscard]] bool isUnreadMention() const;
	[[nodiscard]] bool hasUnreadReaction() const;
	[[nodiscard]] bool hasUnwatchedEffect() const;
	bool markEffectWatched();
	[[nodiscard]] bool isUnreadMedia() const;
	[[nodiscard]] bool isIncomingUnreadMedia() const;
	[[nodiscard]] bool hasUnreadMediaFlag() const;
	void markReactionsRead();
	void markMediaAndMentionRead();
	bool markContentsRead(bool fromThisClient = false);
	void setIsPinned(bool isPinned);
	void setStoryInProfile(bool inProfile);

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
	[[nodiscard]] bool hideDisplayDate() const {
		return isEmpty() || (_flags & MessageFlag::HideDisplayDate);
	}
	[[nodiscard]] bool isLocal() const {
		return _flags & MessageFlag::Local;
	}
	[[nodiscard]] bool isFakeAboutView() const {
		return _flags & MessageFlag::FakeAboutView;
	}
	[[nodiscard]] bool showSimilarChannels() const {
		return _flags & MessageFlag::ShowSimilarChannels;
	}
	[[nodiscard]] bool hasRealFromId() const;
	[[nodiscard]] bool isPostHidingAuthor() const;
	[[nodiscard]] bool isPostShowingAuthor() const;
	[[nodiscard]] bool isRegular() const;
	[[nodiscard]] bool isUploading() const;
	void sendFailed();
	[[nodiscard]] int viewsCount() const;
	[[nodiscard]] int repliesCount() const;
	[[nodiscard]] bool repliesAreComments() const;
	[[nodiscard]] bool externalReply() const;
	[[nodiscard]] bool hasUnpaidContent() const;
	[[nodiscard]] bool inHighlightProcess() const;
	void highlightProcessDone();
	[[nodiscard]] PaidPostType paidType() const;

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
	void applyEdition(const QVector<MTPMessageExtendedMedia> &media);
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
	void overrideMedia(std::unique_ptr<Data::Media> media);

	void applyEditionToHistoryCleared();
	void updateReplyMarkup(
		HistoryMessageMarkupData &&markup,
		bool ignoreSuggestButtons = false);
	void contributeToSlowmode(TimeId realDate = 0);

	void clearMediaAsExpired();

	void addToUnreadThings(HistoryUnreadThings::AddType type);
	void destroyHistoryEntry();
	[[nodiscard]] Storage::SharedMediaTypesMask sharedMediaTypes() const;

	void indexAsNewItem();
	void addToSharedMediaIndex();
	void addToMessagesIndex();
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
	void applyEffectWatchedOnUnreadKnown();

	[[nodiscard]] bool emptyText() const {
		return _text.empty();
	}

	[[nodiscard]] bool canPin() const;
	[[nodiscard]] bool canBeEdited() const;
	[[nodiscard]] bool canStopPoll() const;
	[[nodiscard]] bool forbidsForward() const;
	[[nodiscard]] bool forbidsSaving() const;
	[[nodiscard]] bool allowsSendNow() const;
	[[nodiscard]] bool allowsReschedule() const;
	[[nodiscard]] bool allowsForward() const;
	[[nodiscard]] bool allowsEdit(TimeId now) const;
	[[nodiscard]] bool allowsEditMedia() const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canDeleteForEveryone(TimeId now) const;
	[[nodiscard]] bool suggestReport() const;
	[[nodiscard]] bool suggestBanReport() const;
	[[nodiscard]] bool suggestDeleteAllReport() const;
	[[nodiscard]] ChatRestriction requiredSendRight() const;
	[[nodiscard]] bool requiresSendInlineRight() const;
	[[nodiscard]] Data::SendError errorTextForForward(
		not_null<Data::Thread*> to) const;
	[[nodiscard]] Data::SendError errorTextForForwardIgnoreRights(
		not_null<Data::Thread*> to) const;
	[[nodiscard]] const HistoryMessageTranslation *translation() const;
	[[nodiscard]] bool translationShowRequiresCheck(LanguageId to) const;
	bool translationShowRequiresRequest(LanguageId to);
	void translationDone(LanguageId to, TextWithEntities result);

	[[nodiscard]] bool canReact() const;
	void toggleReaction(
		const Data::ReactionId &reaction,
		HistoryReactionSource source);
	void addPaidReaction(int count, std::optional<PeerId> shownPeer = {});
	void cancelScheduledPaidReaction();
	[[nodiscard]] Data::PaidReactionSend startPaidReactionSending();
	void finishPaidReactionSending(
		Data::PaidReactionSend send,
		bool success);
	void updateReactionsUnknown();
	[[nodiscard]] auto reactions() const
		-> const std::vector<Data::MessageReaction> &;
	[[nodiscard]] auto reactionsWithLocal() const
		-> std::vector<Data::MessageReaction>;
	[[nodiscard]] auto recentReactions() const
		-> const base::flat_map<
			Data::ReactionId,
			std::vector<Data::RecentReaction>> &;
	[[nodiscard]] auto topPaidReactionsWithLocal() const
		-> std::vector<Data::MessageReactionsTopPaid>;
	[[nodiscard]] int reactionsPaidScheduled() const;
	[[nodiscard]] PeerId reactionsLocalShownPeer() const;
	[[nodiscard]] bool canViewReactions() const;
	[[nodiscard]] std::vector<Data::ReactionId> chosenReactions() const;
	[[nodiscard]] Data::ReactionId lookupUnreadReaction(
		not_null<UserData*> from) const;
	[[nodiscard]] crl::time lastReactionsRefreshTime() const;

	[[nodiscard]] bool reactionsAreTags() const;
	[[nodiscard]] bool hasDirectLink() const;
	[[nodiscard]] bool changesWallPaper() const;

	[[nodiscard]] FullMsgId fullId() const;
	[[nodiscard]] GlobalMsgId globalId() const;
	[[nodiscard]] Data::MessagePosition position() const;
	[[nodiscard]] TimeId date() const;
	[[nodiscard]] bool awaitingVideoProcessing() const;

	[[nodiscard]] Data::Media *media() const {
		return _media.get();
	}
	[[nodiscard]] bool computeDropForwardedInfo() const;
	void setText(const TextWithEntities &textWithEntities);

	[[nodiscard]] MsgId replyToId() const;
	[[nodiscard]] FullMsgId replyToFullId() const;
	[[nodiscard]] MsgId replyToTop() const;
	[[nodiscard]] MsgId topicRootId() const;
	[[nodiscard]] FullStoryId replyToStory() const;
	[[nodiscard]] FullReplyTo replyTo() const;
	[[nodiscard]] bool inThread(MsgId rootId) const;

	[[nodiscard]] not_null<PeerData*> author() const;

	[[nodiscard]] TimeId originalDate() const;
	[[nodiscard]] PeerData *originalSender() const;
	[[nodiscard]] const HiddenSenderInfo *originalHiddenSenderInfo() const;
	[[nodiscard]] not_null<PeerData*> fromOriginal() const;
	[[nodiscard]] QString originalPostAuthor() const;
	[[nodiscard]] MsgId originalId() const;

	[[nodiscard]] Data::SavedSublist *savedSublist() const;
	[[nodiscard]] PeerId sublistPeerId() const;
	[[nodiscard]] PeerData *savedFromSender() const;
	[[nodiscard]] const HiddenSenderInfo *savedFromHiddenSenderInfo() const;

	[[nodiscard]] const HiddenSenderInfo *displayHiddenSenderInfo() const;

	[[nodiscard]] bool showForwardsFromSender(
		not_null<const HistoryMessageForwarded*> forwarded) const;

	[[nodiscard]] bool isEmpty() const;
	[[nodiscard]] MessageGroupId groupId() const;
	[[nodiscard]] EffectId effectId() const;
	[[nodiscard]] bool hasPossibleRestrictions() const;
	[[nodiscard]] QString computeUnavailableReason() const;
	[[nodiscard]] bool isMediaSensitive() const;

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
	[[nodiscard]] uint8 colorIndex() const;

	// In forwards we show name in sender's color, but the message
	// content uses the color of the original sender.
	[[nodiscard]] PeerData *contentColorsFrom() const;
	[[nodiscard]] uint8 contentColorIndex() const;
	[[nodiscard]] int starsPaid() const;

	[[nodiscard]] std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr);

	void updateDate(TimeId newDate);
	[[nodiscard]] bool canUpdateDate() const;
	void customEmojiRepaint();

	[[nodiscard]] SuggestionActions computeSuggestionActions() const;
	[[nodiscard]] SuggestionActions computeSuggestionActions(
		const HistoryMessageSuggestedPost *suggest) const;
	[[nodiscard]] SuggestionActions computeSuggestionActions(
		bool accepted,
		bool rejected) const;

	[[nodiscard]] bool needsUpdateForVideoQualities(const MTPMessage &data);

	[[nodiscard]] TimeId ttlDestroyAt() const {
		return _ttlDestroyAt;
	}

	[[nodiscard]] int boostsApplied() const {
		return _boostsApplied;
	}

	MsgId id;

private:
	struct CreateConfig;

	HistoryItem(
		not_null<History*> history,
		const HistoryItemCommonFields &fields);

	void createComponentsHelper(HistoryItemCommonFields &&fields);
	void createComponents(CreateConfig &&config);
	void setupForwardedComponent(const CreateConfig &config);
	void applyInitialEffectWatched();

	[[nodiscard]] bool generateLocalEntitiesByReply() const;
	[[nodiscard]] TextWithEntities withLocalEntities(
		const TextWithEntities &textWithEntities) const;
	void setTextValue(TextWithEntities text, bool force = false);
	[[nodiscard]] bool isTooOldForEdit(TimeId now) const;
	[[nodiscard]] bool isLegacyMessage() const {
		return _flags & MessageFlag::Legacy;
	}

	[[nodiscard]] bool checkDiscussionLink(ChannelId id) const;

	void setReplyMarkup(
		HistoryMessageMarkupData &&markup,
		bool ignoreSuggestButtons = false);
	void updateSuggestControls(const HistoryMessageSuggestedPost *suggest);

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
	void updateServiceDependent(bool force = false);
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

	void resolveDependent(not_null<HistoryServiceDependentData*> dependent);
	void resolveDependent(not_null<HistoryMessageReply*> reply);

	[[nodiscard]] TextWithEntities fromLinkText() const;
	[[nodiscard]] ClickHandlerPtr fromLink() const;

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

	[[nodiscard]] PreparedServiceText preparePinnedText();
	[[nodiscard]] PreparedServiceText prepareGameScoreText();
	[[nodiscard]] PreparedServiceText preparePaymentSentText();
	[[nodiscard]] PreparedServiceText prepareStoryMentionText();
	[[nodiscard]] PreparedServiceText prepareInvitedToCallText(
		const std::vector<not_null<UserData*>> &users,
		CallId linkCallId);
	[[nodiscard]] PreparedServiceText prepareCallScheduledText(
		TimeId scheduleDate);
	[[nodiscard]] PreparedServiceText prepareTodoCompletionsText();
	[[nodiscard]] PreparedServiceText prepareTodoAppendTasksText();

	[[nodiscard]] PreparedServiceText composeTodoIncompleted(
		not_null<HistoryServiceTodoCompletions*> done);
	[[nodiscard]] PreparedServiceText composeTodoCompleted(
		not_null<HistoryServiceTodoCompletions*> done);

	[[nodiscard]] PreparedServiceText prepareServiceTextForMessage(
		const MTPMessageMedia &media,
		bool unread);

	void flagSensitiveContent();
	[[nodiscard]] PeerData *computeDisplayFrom() const;

	const not_null<History*> _history;
	const not_null<PeerData*> _from;
	mutable PeerData *_displayFrom = nullptr;
	mutable MessageFlags _flags = 0;

	TextWithEntities _text;

	std::unique_ptr<Data::Media> _media;
	std::unique_ptr<Data::MessageReactions> _reactions;
	crl::time _reactionsLastRefreshed = 0;

	TimeId _date = 0;
	TimeId _ttlDestroyAt = 0;
	int _boostsApplied = 0;
	int _starsPaid = 0;
	BusinessShortcutId _shortcutId = 0;

	MessageGroupId _groupId = MessageGroupId();
	EffectId _effectId = 0;
	HistoryView::Element *_mainView = nullptr;

	friend class HistoryView::Element;
	friend class HistoryView::Message;
	friend class HistoryView::Service;
	friend class HistoryView::ServiceMessagePainter;

};

constexpr auto kSize = int(sizeof(HistoryItem));
