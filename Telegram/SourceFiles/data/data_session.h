/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_databases.h"
#include "dialogs/dialogs_main_list.h"
#include "data/data_groups.h"
#include "data/data_cloud_file.h"
#include "history/history_location_manager.h"
#include "base/timer.h"

class Image;
class HistoryItem;
struct WebPageCollage;
struct WebPageStickerSet;
enum class WebPageType : uint8;
enum class NewMessageType;

namespace HistoryView {
struct Group;
class Element;
class ElementDelegate;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Passport {
struct SavedCredentials;
} // namespace Passport

namespace Iv {
class Data;
} // namespace Iv

namespace Data {

class Folder;
class LocationPoint;
class WallPaper;
class ShortcutMessages;
class SendActionManager;
class Reactions;
class EmojiStatuses;
class ForumIcons;
class ChatFilters;
class CloudThemes;
class Streaming;
class MediaRotation;
class Histories;
class DocumentMedia;
class PhotoMedia;
class Stickers;
class GroupCall;
class NotifySettings;
class CustomEmojiManager;
class Stories;
class SavedMessages;
class Chatbots;
class BusinessInfo;
struct ReactionId;
struct UnavailableReason;

struct RepliesReadTillUpdate {
	FullMsgId id;
	MsgId readTillId;
	bool out = false;
};

struct GiftUpdate {
	enum class Action : uchar {
		Save,
		Unsave,
		Convert,
		Delete,
	};

	FullMsgId itemId;
	Action action = {};
};

class Session final {
public:
	using ViewElement = HistoryView::Element;

	struct SentData {
		PeerId peerId = 0;
		QString text;
	};

	explicit Session(not_null<Main::Session*> session);
	~Session();

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}

	[[nodiscard]] QString nameSortKey(const QString &name) const;

	[[nodiscard]] Groups &groups() {
		return _groups;
	}
	[[nodiscard]] const Groups &groups() const {
		return _groups;
	}
	[[nodiscard]] ChatFilters &chatsFilters() const {
		return *_chatsFilters;
	}
	[[nodiscard]] ShortcutMessages &shortcutMessages() const {
		return *_shortcutMessages;
	}
	[[nodiscard]] SendActionManager &sendActionManager() const {
		return *_sendActionManager;
	}
	[[nodiscard]] CloudThemes &cloudThemes() const {
		return *_cloudThemes;
	}
	[[nodiscard]] Streaming &streaming() const {
		return *_streaming;
	}
	[[nodiscard]] MediaRotation &mediaRotation() const {
		return *_mediaRotation;
	}
	[[nodiscard]] Histories &histories() const {
		return *_histories;
	}
	[[nodiscard]] Stickers &stickers() const {
		return *_stickers;
	}
	[[nodiscard]] Reactions &reactions() const {
		return *_reactions;
	}
	[[nodiscard]] EmojiStatuses &emojiStatuses() const {
		return *_emojiStatuses;
	}
	[[nodiscard]] ForumIcons &forumIcons() const {
		return *_forumIcons;
	}
	[[nodiscard]] NotifySettings &notifySettings() const {
		return *_notifySettings;
	}
	[[nodiscard]] CustomEmojiManager &customEmojiManager() const {
		return *_customEmojiManager;
	}
	[[nodiscard]] Stories &stories() const {
		return *_stories;
	}
	[[nodiscard]] SavedMessages &savedMessages() const {
		return *_savedMessages;
	}
	[[nodiscard]] Chatbots &chatbots() const {
		return *_chatbots;
	}
	[[nodiscard]] BusinessInfo &businessInfo() const {
		return *_businessInfo;
	}

	[[nodiscard]] MsgId nextNonHistoryEntryId() {
		return ++_nonHistoryEntryId;
	}

	void subscribeForTopicRepliesLists();
	void clear();

	void keepAlive(std::shared_ptr<PhotoMedia> media);
	void keepAlive(std::shared_ptr<DocumentMedia> media);

	void suggestStartExport(TimeId availableAt);
	void clearExportSuggestion();

	[[nodiscard]] auto passportCredentials() const
	-> const Passport::SavedCredentials*;
	void rememberPassportCredentials(
		Passport::SavedCredentials data,
		crl::time rememberFor);
	void forgetPassportCredentials();

	[[nodiscard]] Storage::Cache::Database &cache();
	[[nodiscard]] Storage::Cache::Database &cacheBigFile();

	[[nodiscard]] not_null<PeerData*> peer(PeerId id);
	[[nodiscard]] not_null<PeerData*> peer(UserId id) = delete;
	[[nodiscard]] not_null<UserData*> user(UserId id);
	[[nodiscard]] not_null<ChatData*> chat(ChatId id);
	[[nodiscard]] not_null<ChannelData*> channel(ChannelId id);
	[[nodiscard]] not_null<UserData*> user(PeerId id) = delete;
	[[nodiscard]] not_null<ChatData*> chat(PeerId id) = delete;
	[[nodiscard]] not_null<ChannelData*> channel(PeerId id) = delete;

	[[nodiscard]] PeerData *peerLoaded(PeerId id) const;
	[[nodiscard]] PeerData *peerLoaded(UserId id) const = delete;
	[[nodiscard]] UserData *userLoaded(UserId id) const;
	[[nodiscard]] ChatData *chatLoaded(ChatId id) const;
	[[nodiscard]] ChannelData *channelLoaded(ChannelId id) const;
	[[nodiscard]] UserData *userLoaded(PeerId id) const = delete;
	[[nodiscard]] ChatData *chatLoaded(PeerId id) const = delete;
	[[nodiscard]] ChannelData *channelLoaded(PeerId id) const = delete;

	not_null<UserData*> processUser(const MTPUser &data);
	not_null<PeerData*> processChat(const MTPChat &data);

	// Returns last user, if there were any.
	UserData *processUsers(const MTPVector<MTPUser> &data);
	PeerData *processChats(const MTPVector<MTPChat> &data);

	void applyMaximumChatVersions(const MTPVector<MTPChat> &data);

	void registerGroupCall(not_null<GroupCall*> call);
	void unregisterGroupCall(not_null<GroupCall*> call);
	GroupCall *groupCall(CallId callId) const;

	void watchForOffline(not_null<UserData*> user, TimeId now = 0);
	void maybeStopWatchForOffline(not_null<UserData*> user);

	[[nodiscard]] auto invitedToCallUsers(CallId callId) const
		-> const base::flat_set<not_null<UserData*>> &;
	void registerInvitedToCallUser(
		CallId callId,
		not_null<PeerData*> peer,
		not_null<UserData*> user);
	void unregisterInvitedToCallUser(CallId callId, not_null<UserData*> user);

	struct InviteToCall {
		CallId id = 0;
		not_null<UserData*> user;
	};
	[[nodiscard]] rpl::producer<InviteToCall> invitesToCalls() const {
		return _invitesToCalls.events();
	}

	void enumerateUsers(Fn<void(not_null<UserData*>)> action) const;
	void enumerateGroups(Fn<void(not_null<PeerData*>)> action) const;
	void enumerateBroadcasts(Fn<void(not_null<ChannelData*>)> action) const;
	[[nodiscard]] UserData *userByPhone(const QString &phone) const;
	[[nodiscard]] PeerData *peerByUsername(const QString &username) const;

	[[nodiscard]] not_null<History*> history(PeerId peerId);
	[[nodiscard]] History *historyLoaded(PeerId peerId) const;
	[[nodiscard]] not_null<History*> history(UserId userId) = delete;
	[[nodiscard]] History *historyLoaded(UserId userId) const = delete;
	[[nodiscard]] not_null<History*> history(not_null<const PeerData*> peer);
	[[nodiscard]] History *historyLoaded(const PeerData *peer);

	void deleteConversationLocally(not_null<PeerData*> peer);

	[[nodiscard]] rpl::variable<bool> &contactsLoaded() {
		return _contactsLoaded;
	}
	[[nodiscard]] rpl::producer<Folder*> chatsListChanges() const {
		return _chatsListChanged.events();
	}
	[[nodiscard]] bool chatsListLoaded(Folder *folder = nullptr);
	[[nodiscard]] rpl::producer<Folder*> chatsListLoadedEvents() const {
		return _chatsListLoadedEvents.events();
	}
	void chatsListChanged(FolderId folderId);
	void chatsListChanged(Folder *folder);
	void chatsListDone(Folder *folder);

	void userIsBotChanged(not_null<UserData*> user);
	[[nodiscard]] rpl::producer<not_null<UserData*>> userIsBotChanges() const;
	void botCommandsChanged(not_null<PeerData*> peer);
	[[nodiscard]] rpl::producer<not_null<PeerData*>> botCommandsChanges() const;

	struct ItemVisibilityQuery {
		not_null<HistoryItem*> item;
		not_null<bool*> isVisible;
	};
	[[nodiscard]] bool queryItemVisibility(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool queryDocumentVisibility(not_null<DocumentData*> document) const;
	[[nodiscard]] rpl::producer<ItemVisibilityQuery> itemVisibilityQueries() const;
	void itemVisibilitiesUpdated();

	struct IdChange {
		FullMsgId newId;
		MsgId oldId = 0;
	};
	void notifyItemIdChange(IdChange event);
	[[nodiscard]] rpl::producer<IdChange> itemIdChanged() const;
	void notifyItemLayoutChange(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemLayoutChanged() const;
	void notifyViewLayoutChange(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewLayoutChanged() const;
	void notifyNewItemAdded(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> newItemAdded() const;
	void notifyGiftUpdate(GiftUpdate &&update);
	[[nodiscard]] rpl::producer<GiftUpdate> giftUpdates() const;
	void requestItemRepaint(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRepaintRequest() const;
	void requestViewRepaint(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRepaintRequest() const;
	void requestItemResize(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemResizeRequest() const;
	void requestViewResize(not_null<ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<ViewElement*>> viewResizeRequest() const;
	void requestItemViewRefresh(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemViewRefreshRequest() const;
	void requestItemTextRefresh(not_null<HistoryItem*> item);
	void requestUnreadReactionsAnimation(not_null<HistoryItem*> item);
	void notifyHistoryUnloaded(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyUnloaded() const;
	void notifyItemDataChange(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> itemDataChanges() const;

	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRemoved() const;
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRemoved(
		FullMsgId itemId) const;
	void notifyViewRemoved(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRemoved() const;
	void notifyHistoryCleared(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyCleared() const;
	void notifyHistoryChangeDelayed(not_null<History*> history);
	[[nodiscard]] rpl::producer<not_null<History*>> historyChanged() const;
	void notifyViewPaidReactionSent(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewPaidReactionSent() const;
	void sendHistoryChangeNotifications();

	void notifyPinnedDialogsOrderUpdated();
	[[nodiscard]] rpl::producer<> pinnedDialogsOrderUpdated() const;

	void registerRestricted(
		not_null<const HistoryItem*> item,
		const QString &reason);
	void registerRestricted(
		not_null<const HistoryItem*> item,
		const std::vector<UnavailableReason> &reasons);

	void registerHighlightProcess(
		uint64 processId,
		not_null<HistoryItem*> item);

	void registerHeavyViewPart(not_null<ViewElement*> view);
	void unregisterHeavyViewPart(not_null<ViewElement*> view);
	void unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate);
	void unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate,
		int from,
		int till);

	void registerShownSpoiler(not_null<ViewElement*> view);
	void hideShownSpoilers();

	using MegagroupParticipant = std::tuple<
		not_null<ChannelData*>,
		not_null<UserData*>>;
	void removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] rpl::producer<MegagroupParticipant> megagroupParticipantRemoved() const;
	[[nodiscard]] rpl::producer<not_null<UserData*>> megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const;
	void addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] rpl::producer<MegagroupParticipant> megagroupParticipantAdded() const;
	[[nodiscard]] rpl::producer<not_null<UserData*>> megagroupParticipantAdded(
		not_null<ChannelData*> channel) const;

	HistoryItemsList idsToItems(const MessageIdsList &ids) const;
	MessageIdsList itemsToIds(const HistoryItemsList &items) const;
	MessageIdsList itemOrItsGroup(not_null<HistoryItem*> item) const;

	void applyUpdate(const MTPDupdateMessagePoll &update);
	void applyUpdate(const MTPDupdateChatParticipants &update);
	void applyUpdate(const MTPDupdateChatParticipantAdd &update);
	void applyUpdate(const MTPDupdateChatParticipantDelete &update);
	void applyUpdate(const MTPDupdateChatParticipantAdmin &update);
	void applyUpdate(const MTPDupdateChatDefaultBannedRights &update);

	void applyDialogs(
		Folder *requestFolder,
		const QVector<MTPMessage> &messages,
		const QVector<MTPDialog> &dialogs,
		std::optional<int> count = std::nullopt);

	[[nodiscard]] bool pinnedCanPin(not_null<Dialogs::Entry*> entry) const;
	[[nodiscard]] bool pinnedCanPin(
		FilterId filterId,
		not_null<History*> history) const;
	[[nodiscard]] int pinnedChatsLimit(Folder *folder) const;
	[[nodiscard]] int pinnedChatsLimit(FilterId filterId) const;
	[[nodiscard]] int pinnedChatsLimit(not_null<Forum*> forum) const;
	[[nodiscard]] int pinnedChatsLimit(
		not_null<SavedMessages*> saved) const;
	[[nodiscard]] rpl::producer<int> maxPinnedChatsLimitValue(
		Folder *folder) const;
	[[nodiscard]] rpl::producer<int> maxPinnedChatsLimitValue(
		FilterId filterId) const;
	[[nodiscard]] rpl::producer<int> maxPinnedChatsLimitValue(
		not_null<Forum*> forum) const;
	[[nodiscard]] rpl::producer<int> maxPinnedChatsLimitValue(
		not_null<SavedMessages*> saved) const;
	[[nodiscard]] int groupFreeTranscribeLevel() const;
	[[nodiscard]] const std::vector<Dialogs::Key> &pinnedChatsOrder(
		Folder *folder) const;
	[[nodiscard]] const std::vector<Dialogs::Key> &pinnedChatsOrder(
		not_null<Forum*> forum) const;
	[[nodiscard]] const std::vector<Dialogs::Key> &pinnedChatsOrder(
		FilterId filterId) const;
	[[nodiscard]] const std::vector<Dialogs::Key> &pinnedChatsOrder(
		not_null<Data::SavedMessages*> saved) const;
	void setChatPinned(Dialogs::Key key, FilterId filterId, bool pinned);
	void setPinnedFromEntryList(Dialogs::Key key, bool pinned);
	void clearPinnedChats(Folder *folder);
	void applyPinnedChats(
		Folder *folder,
		const QVector<MTPDialogPeer> &list);
	void applyPinnedTopics(
		not_null<Data::Forum*> forum,
		const QVector<MTPint> &list);
	void reorderTwoPinnedChats(
		FilterId filterId,
		Dialogs::Key key1,
		Dialogs::Key key2);

	void setSuggestToGigagroup(not_null<ChannelData*> group, bool suggest);
	[[nodiscard]] bool suggestToGigagroup(
		not_null<ChannelData*> group) const;

	void registerMessage(not_null<HistoryItem*> item);
	void unregisterMessage(not_null<HistoryItem*> item);

	void registerMessageTTL(TimeId when, not_null<HistoryItem*> item);
	void unregisterMessageTTL(TimeId when, not_null<HistoryItem*> item);

	// Returns true if item found and it is not detached.
	bool updateExistingMessage(const MTPDmessage &data);
	void updateEditedMessage(const MTPMessage &data);
	void processMessages(
		const QVector<MTPMessage> &data,
		NewMessageType type);
	void processMessages(
		const MTPVector<MTPMessage> &data,
		NewMessageType type);
	void processExistingMessages(
		ChannelData *channel,
		const MTPmessages_Messages &data);
	void processNonChannelMessagesDeleted(const QVector<MTPint> &data);
	void processMessagesDeleted(
		PeerId peerId,
		const QVector<MTPint> &data);

	[[nodiscard]] MsgId nextLocalMessageId();
	[[nodiscard]] HistoryItem *message(
		PeerId peerId,
		MsgId itemId) const;
	[[nodiscard]] HistoryItem *message(
		not_null<const PeerData*> peer,
		MsgId itemId) const;
	[[nodiscard]] HistoryItem *message(FullMsgId itemId) const;

	[[nodiscard]] HistoryItem *nonChannelMessage(MsgId itemId) const;

	void updateDependentMessages(not_null<HistoryItem*> item);
	void registerDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency);
	void unregisterDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency);

	void destroyAllCallItems();

	void registerMessageRandomId(uint64 randomId, FullMsgId itemId);
	void unregisterMessageRandomId(uint64 randomId);
	[[nodiscard]] FullMsgId messageIdByRandomId(uint64 randomId) const;
	void registerMessageSentData(
		uint64 randomId,
		PeerId peerId,
		const QString &text);
	void unregisterMessageSentData(uint64 randomId);
	[[nodiscard]] SentData messageSentData(uint64 randomId) const;

	void photoLoadSettingsChanged();
	void documentLoadSettingsChanged();

	void notifyPhotoLayoutChanged(not_null<const PhotoData*> photo);
	void requestPhotoViewRepaint(not_null<const PhotoData*> photo);
	void notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document);
	void requestDocumentViewRepaint(not_null<const DocumentData*> document);
	void markMediaRead(not_null<const DocumentData*> document);
	void requestPollViewRepaint(not_null<const PollData*> poll);

	void photoLoadProgress(not_null<PhotoData*> photo);
	void photoLoadDone(not_null<PhotoData*> photo);
	void photoLoadFail(not_null<PhotoData*> photo, bool started);

	void documentLoadProgress(not_null<DocumentData*> document);
	void documentLoadDone(not_null<DocumentData*> document);
	void documentLoadFail(not_null<DocumentData*> document, bool started);

	[[nodiscard]] auto documentLoadProgress() const
	-> rpl::producer<not_null<DocumentData*>> {
		return _documentLoadProgress.events();
	}

	HistoryItem *addNewMessage(
		const MTPMessage &data,
		MessageFlags localFlags,
		NewMessageType type);
	HistoryItem *addNewMessage( // Override message id.
		MsgId id,
		const MTPMessage &data,
		MessageFlags localFlags,
		NewMessageType type);

	[[nodiscard]] int unreadBadge() const;
	[[nodiscard]] bool unreadBadgeMuted() const;
	[[nodiscard]] int unreadBadgeIgnoreOne(Dialogs::Key key) const;
	[[nodiscard]] bool unreadBadgeMutedIgnoreOne(Dialogs::Key key) const;
	[[nodiscard]] int unreadOnlyMutedBadge() const;
	[[nodiscard]] rpl::producer<> unreadBadgeChanges() const;
	void notifyUnreadBadgeChanged();

	void updateRepliesReadTill(RepliesReadTillUpdate update);
	[[nodiscard]] auto repliesReadTillUpdates() const
		-> rpl::producer<RepliesReadTillUpdate>;

	void selfDestructIn(not_null<HistoryItem*> item, crl::time delay);

	[[nodiscard]] not_null<PhotoData*> photo(PhotoId id);
	not_null<PhotoData*> processPhoto(const MTPPhoto &data);
	not_null<PhotoData*> processPhoto(const MTPDphoto &data);
	not_null<PhotoData*> processPhoto(
		const MTPPhoto &data,
		const PreparedPhotoThumbs &thumbs);
	[[nodiscard]] not_null<PhotoData*> photo(
		PhotoId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		int32 dc,
		bool hasStickers,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
		crl::time videoStartTime);
	void photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data);
	[[nodiscard]] PhotoData *photoFromWeb(
		const MTPWebDocument &data,
		const ImageLocation &thumbnailLocation);

	[[nodiscard]] not_null<DocumentData*> document(DocumentId id);
	not_null<DocumentData*> processDocument(const MTPDocument &data);
	not_null<DocumentData*> processDocument(const MTPDdocument &data);
	not_null<DocumentData*> processDocument(
		const MTPdocument &data,
		const ImageWithLocation &thumbnail);
	[[nodiscard]] not_null<DocumentData*> document(
		DocumentId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker,
		int32 dc,
		int64 size);
	void documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data);
	[[nodiscard]] DocumentData *documentFromWeb(
		const MTPWebDocument &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation);
	[[nodiscard]] not_null<DocumentData*> venueIconDocument(
		const QString &icon);

	[[nodiscard]] not_null<WebPageData*> webpage(WebPageId id);
	not_null<WebPageData*> processWebpage(const MTPWebPage &data);
	not_null<WebPageData*> processWebpage(const MTPDwebPage &data);
	not_null<WebPageData*> processWebpage(const MTPDwebPagePending &data);
	[[nodiscard]] not_null<WebPageData*> webpage(
		WebPageId id,
		const QString &siteName,
		const TextWithEntities &content);
	[[nodiscard]] not_null<WebPageData*> webpage(
		WebPageId id,
		WebPageType type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		std::unique_ptr<Iv::Data> iv,
		std::unique_ptr<WebPageStickerSet> stickerSet,
		int duration,
		const QString &author,
		bool hasLargeMedia,
		TimeId pendingTill);

	[[nodiscard]] not_null<GameData*> game(GameId id);
	not_null<GameData*> processGame(const MTPDgame &data);
	[[nodiscard]] not_null<GameData*> game(
		GameId id,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document);
	void gameConvert(
		not_null<GameData*> original,
		const MTPGame &data);

	[[nodiscard]] not_null<BotAppData*> botApp(BotAppId id);
	BotAppData *findBotApp(PeerId botId, const QString &appName) const;
	BotAppData *processBotApp(
		PeerId botId,
		const MTPBotApp &data);

	[[nodiscard]] not_null<PollData*> poll(PollId id);
	not_null<PollData*> processPoll(const MTPPoll &data);
	not_null<PollData*> processPoll(const MTPDmessageMediaPoll &data);

	[[nodiscard]] not_null<CloudImage*> location(
		const LocationPoint &point);

	void registerPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item);
	void unregisterPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item);
	void registerDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item);
	void unregisterDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item);
	void registerWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view);
	void unregisterWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view);
	void registerWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item);
	void unregisterWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item);
	void registerGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view);
	void unregisterGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view);
	void registerPollView(
		not_null<const PollData*> poll,
		not_null<ViewElement*> view);
	void unregisterPollView(
		not_null<const PollData*> poll,
		not_null<ViewElement*> view);
	void registerContactView(
		UserId contactId,
		not_null<ViewElement*> view);
	void unregisterContactView(
		UserId contactId,
		not_null<ViewElement*> view);
	void registerContactItem(
		UserId contactId,
		not_null<HistoryItem*> item);
	void unregisterContactItem(
		UserId contactId,
		not_null<HistoryItem*> item);
	void registerCallItem(not_null<HistoryItem*> item);
	void unregisterCallItem(not_null<HistoryItem*> item);
	void registerStoryItem(FullStoryId id, not_null<HistoryItem*> item);
	void unregisterStoryItem(FullStoryId id, not_null<HistoryItem*> item);
	void refreshStoryItemViews(FullStoryId id);

	void documentMessageRemoved(not_null<DocumentData*> document);

	void checkPlayingAnimations();

	HistoryItem *findWebPageItem(not_null<WebPageData*> page) const;
	QString findContactPhone(not_null<UserData*> contact) const;
	QString findContactPhone(UserId contactId) const;

	void notifyWebPageUpdateDelayed(not_null<WebPageData*> page);
	void notifyGameUpdateDelayed(not_null<GameData*> game);
	void notifyPollUpdateDelayed(not_null<PollData*> poll);
	[[nodiscard]] bool hasPendingWebPageGamePollNotification() const;
	void sendWebPageGamePollNotifications();
	[[nodiscard]] rpl::producer<not_null<WebPageData*>> webPageUpdates() const;

	void channelDifferenceTooLong(not_null<ChannelData*> channel);
	[[nodiscard]] rpl::producer<not_null<ChannelData*>> channelDifferenceTooLong() const;

	void registerItemView(not_null<ViewElement*> view);
	void unregisterItemView(not_null<ViewElement*> view);

	[[nodiscard]] not_null<Folder*> folder(FolderId id);
	[[nodiscard]] Folder *folderLoaded(FolderId id) const;
	not_null<Folder*> processFolder(const MTPFolder &data);
	not_null<Folder*> processFolder(const MTPDfolder &data);

	[[nodiscard]] not_null<Dialogs::MainList*> chatsListFor(
		not_null<Dialogs::Entry*> entry);
	[[nodiscard]] not_null<Dialogs::MainList*> chatsList(
		Folder *folder = nullptr);
	[[nodiscard]] not_null<const Dialogs::MainList*> chatsList(
		Folder *folder = nullptr) const;
	[[nodiscard]] not_null<Dialogs::IndexedList*> contactsList();
	[[nodiscard]] not_null<Dialogs::IndexedList*> contactsNoChatsList();

	struct ChatListEntryRefresh {
		Dialogs::Key key;
		Dialogs::PositionChange moved;
		FilterId filterId = 0;
		bool existenceChanged = false;

		explicit operator bool() const {
			return existenceChanged || (moved.from != moved.to);
		}
	};
	void refreshChatListEntry(Dialogs::Key key);
	void removeChatListEntry(Dialogs::Key key);
	[[nodiscard]] auto chatListEntryRefreshes() const
		-> rpl::producer<ChatListEntryRefresh>;

	struct DialogsRowReplacement {
		not_null<Dialogs::Row*> old;
		Dialogs::Row *now = nullptr;
	};
	void dialogsRowReplaced(DialogsRowReplacement replacement);
	rpl::producer<DialogsRowReplacement> dialogsRowReplacements() const;

	void serviceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media = MTP_messageMediaEmpty(),
		bool invertMedia = false);

	void setMimeForwardIds(MessageIdsList &&list);
	MessageIdsList takeMimeForwardIds();

	void setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message);

	bool updateWallpapers(const MTPaccount_WallPapers &data);
	void removeWallpaper(const WallPaper &paper);
	const std::vector<WallPaper> &wallpapers() const;
	uint64 wallpapersHash() const;

	struct WebViewResultSent {
		uint64 queryId = 0;
	};
	void webViewResultSent(WebViewResultSent &&sent);
	[[nodiscard]] rpl::producer<WebViewResultSent> webViewResultSent() const;

	void saveViewAsMessages(not_null<Forum*> forum, bool viewAsMessages);

	[[nodiscard]] auto peerDecorationsUpdated() const
		-> rpl::producer<not_null<PeerData*>>;

	void applyStatsDcId(not_null<PeerData*>, MTP::DcId);
	[[nodiscard]] MTP::DcId statsDcId(not_null<PeerData*>);

	void viewTagsChanged(
		not_null<ViewElement*> view,
		std::vector<ReactionId> &&was,
		std::vector<ReactionId> &&now);

	void clearLocalStorage();

private:
	using Messages = std::unordered_map<MsgId, not_null<HistoryItem*>>;

	void suggestStartExport();

	void setupMigrationViewer();
	void setupChannelLeavingViewer();
	void setupPeerNameViewer();
	void setupUserIsContactViewer();

	void checkSelfDestructItems();
	void checkLocalUsersWentOffline();

	void scheduleNextTTLs();
	void checkTTLs();

	int computeUnreadBadge(const Dialogs::UnreadState &state) const;
	bool computeUnreadBadgeMuted(const Dialogs::UnreadState &state) const;

	void applyDialog(Folder *requestFolder, const MTPDdialog &data);
	void applyDialog(
		Folder *requestFolder,
		const MTPDdialogFolder &data);

	const Messages *messagesList(PeerId peerId) const;
	not_null<Messages*> messagesListForInsert(PeerId peerId);
	not_null<HistoryItem*> registerMessage(
		std::unique_ptr<HistoryItem> item);
	HistoryItem *changeMessageId(PeerId peerId, MsgId wasId, MsgId nowId);
	void removeDependencyMessage(not_null<HistoryItem*> item);

	void photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPPhoto &data);
	void photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPDphoto &data);
	void photoApplyFields(
		not_null<PhotoData*> photo,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		int32 dc,
		bool hasStickers,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
		crl::time videoStartTime);

	void documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDocument &data);
	void documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDdocument &data);
	void documentApplyFields(
		not_null<DocumentData*> document,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const InlineImageLocation &inlineThumbnail,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		bool isPremiumSticker,
		int32 dc,
		int64 size);
	DocumentData *documentFromWeb(
		const MTPDwebDocument &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation);
	DocumentData *documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation);

	void webpageApplyFields(
		not_null<WebPageData*> page,
		const MTPDwebPage &data);
	void webpageApplyFields(
		not_null<WebPageData*> page,
		WebPageType type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		FullStoryId storyId,
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		std::unique_ptr<Iv::Data> iv,
		std::unique_ptr<WebPageStickerSet> stickerSet,
		int duration,
		const QString &author,
		bool hasLargeMedia,
		TimeId pendingTill);

	void gameApplyFields(
		not_null<GameData*> game,
		const MTPDgame &data);
	void gameApplyFields(
		not_null<GameData*> game,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document);

	template <typename Method>
	void enumerateItemViews(
		not_null<const HistoryItem*> item,
		Method method);

	void insertCheckedServiceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media,
		TimeId date,
		bool invertMedia);

	void setWallpapers(const QVector<MTPWallPaper> &data, uint64 hash);
	void highlightProcessDone(uint64 processId);

	void checkPollsClosings();

	const not_null<Main::Session*> _session;

	Storage::DatabasePointer _cache;
	Storage::DatabasePointer _bigFileCache;

	TimeId _exportAvailableAt = 0;
	QPointer<Ui::BoxContent> _exportSuggestion;

	rpl::variable<bool> _contactsLoaded = false;
	rpl::variable<int> _groupFreeTranscribeLevel;
	rpl::event_stream<Folder*> _chatsListLoadedEvents;
	rpl::event_stream<Folder*> _chatsListChanged;
	rpl::event_stream<not_null<UserData*>> _userIsBotChanges;
	rpl::event_stream<not_null<PeerData*>> _botCommandsChanges;
	rpl::event_stream<ItemVisibilityQuery> _itemVisibilityQueries;
	rpl::event_stream<IdChange> _itemIdChanges;
	rpl::event_stream<not_null<const HistoryItem*>> _itemLayoutChanges;
	rpl::event_stream<not_null<const ViewElement*>> _viewLayoutChanges;
	rpl::event_stream<not_null<HistoryItem*>> _newItemAdded;
	rpl::event_stream<GiftUpdate> _giftUpdates;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRepaintRequest;
	rpl::event_stream<not_null<const ViewElement*>> _viewRepaintRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemResizeRequest;
	rpl::event_stream<not_null<ViewElement*>> _viewResizeRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemViewRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemTextRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemDataChanges;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRemoved;
	rpl::event_stream<not_null<const ViewElement*>> _viewRemoved;
	rpl::event_stream<not_null<const ViewElement*>> _viewPaidReactionSent;
	rpl::event_stream<not_null<const History*>> _historyUnloaded;
	rpl::event_stream<not_null<const History*>> _historyCleared;
	base::flat_set<not_null<History*>> _historiesChanged;
	rpl::event_stream<not_null<History*>> _historyChanged;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantRemoved;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantAdded;
	rpl::event_stream<DialogsRowReplacement> _dialogsRowReplacements;
	rpl::event_stream<ChatListEntryRefresh> _chatListEntryRefreshes;
	rpl::event_stream<> _unreadBadgeChanges;
	rpl::event_stream<RepliesReadTillUpdate> _repliesReadTillUpdates;

	Dialogs::MainList _chatsList;
	Dialogs::IndexedList _contactsList;
	Dialogs::IndexedList _contactsNoChatsList;

	MsgId _localMessageIdCounter = StartClientMsgId;
	std::unordered_map<PeerId, Messages> _messages;
	std::map<
		not_null<HistoryItem*>,
		base::flat_set<not_null<HistoryItem*>>> _dependentMessages;
	std::map<TimeId, base::flat_set<not_null<HistoryItem*>>> _ttlMessages;
	base::Timer _ttlCheckTimer;

	std::unordered_map<MsgId, not_null<HistoryItem*>> _nonChannelMessages;

	base::flat_map<uint64, FullMsgId> _messageByRandomId;
	base::flat_map<uint64, SentData> _sentMessagesData;

	base::Timer _selfDestructTimer;
	std::vector<FullMsgId> _selfDestructItems;

	std::unordered_map<
		PhotoId,
		std::unique_ptr<PhotoData>> _photos;
	std::unordered_map<
		not_null<const PhotoData*>,
		base::flat_set<not_null<HistoryItem*>>> _photoItems;
	std::unordered_map<
		DocumentId,
		std::unique_ptr<DocumentData>> _documents;
	std::unordered_map<
		not_null<const DocumentData*>,
		base::flat_set<not_null<HistoryItem*>>> _documentItems;
	std::unordered_map<
		WebPageId,
		std::unique_ptr<WebPageData>> _webpages;
	std::unordered_map<
		not_null<const WebPageData*>,
		base::flat_set<not_null<HistoryItem*>>> _webpageItems;
	std::unordered_map<
		not_null<const WebPageData*>,
		base::flat_set<not_null<ViewElement*>>> _webpageViews;
	std::unordered_map<
		LocationPoint,
		std::unique_ptr<CloudImage>> _locations;
	std::unordered_map<
		PollId,
		std::unique_ptr<PollData>> _polls;
	std::unordered_map<
		GameId,
		std::unique_ptr<GameData>> _games;
	std::unordered_map<
		BotAppId,
		std::unique_ptr<BotAppData>> _botApps;
	std::unordered_map<
		not_null<const GameData*>,
		base::flat_set<not_null<ViewElement*>>> _gameViews;
	std::unordered_map<
		not_null<const PollData*>,
		base::flat_set<not_null<ViewElement*>>> _pollViews;
	std::unordered_map<
		UserId,
		base::flat_set<not_null<HistoryItem*>>> _contactItems;
	std::unordered_map<
		UserId,
		base::flat_set<not_null<ViewElement*>>> _contactViews;
	std::unordered_set<not_null<HistoryItem*>> _callItems;
	std::unordered_map<
		FullStoryId,
		base::flat_set<not_null<HistoryItem*>>> _storyItems;
	base::flat_map<uint64, not_null<HistoryItem*>> _highlightings;
	base::flat_map<QString, not_null<DocumentData*>> _venueIcons;

	base::flat_set<not_null<WebPageData*>> _webpagesUpdated;
	base::flat_set<not_null<GameData*>> _gamesUpdated;
	base::flat_set<not_null<PollData*>> _pollsUpdated;

	rpl::event_stream<not_null<WebPageData*>> _webpageUpdates;
	rpl::event_stream<not_null<ChannelData*>> _channelDifferenceTooLong;
	rpl::event_stream<not_null<DocumentData*>> _documentLoadProgress;
	base::flat_set<not_null<ChannelData*>> _suggestToGigagroup;

	base::flat_multi_map<TimeId, not_null<PollData*>> _pollsClosings;
	base::Timer _pollsClosingTimer;

	base::flat_map<
		not_null<const HistoryItem*>,
		base::flat_set<QString>> _possiblyRestricted;

	base::flat_map<FolderId, std::unique_ptr<Folder>> _folders;

	std::unordered_map<
		not_null<const HistoryItem*>,
		std::vector<not_null<ViewElement*>>> _views;

	rpl::event_stream<> _pinnedDialogsOrderUpdated;

	base::flat_set<not_null<ViewElement*>> _heavyViewParts;

	base::flat_map<uint64, not_null<GroupCall*>> _groupCalls;
	rpl::event_stream<InviteToCall> _invitesToCalls;
	base::flat_map<
		uint64,
		base::flat_set<not_null<UserData*>>> _invitedToCallUsers;

	base::flat_set<not_null<ViewElement*>> _shownSpoilers;
	base::flat_map<
		ReactionId,
		base::flat_set<not_null<ViewElement*>>> _viewsByTag;

	History *_topPromoted = nullptr;

	std::unordered_map<PeerId, std::unique_ptr<PeerData>> _peers;

	MessageIdsList _mimeForwardIds;

	using CredentialsWithGeneration = std::pair<
		const Passport::SavedCredentials,
		int>;
	std::unique_ptr<CredentialsWithGeneration> _passportCredentials;

	std::vector<WallPaper> _wallpapers;
	uint64 _wallpapersHash = 0;

	base::flat_map<not_null<UserData*>, TimeId> _watchingForOffline;
	base::Timer _watchForOfflineTimer;

	base::flat_map<not_null<PeerData*>, MTP::DcId> _peerStatsDcIds;

	rpl::event_stream<WebViewResultSent> _webViewResultSent;

	rpl::event_stream<not_null<PeerData*>> _peerDecorationsUpdated;
	base::flat_map<
		not_null<ChannelData*>,
		mtpRequestId> _viewAsMessagesRequests;

	Groups _groups;
	const std::unique_ptr<ChatFilters> _chatsFilters;
	const std::unique_ptr<CloudThemes> _cloudThemes;
	const std::unique_ptr<SendActionManager> _sendActionManager;
	const std::unique_ptr<Streaming> _streaming;
	const std::unique_ptr<MediaRotation> _mediaRotation;
	const std::unique_ptr<Histories> _histories;
	const std::unique_ptr<Stickers> _stickers;
	const std::unique_ptr<Reactions> _reactions;
	const std::unique_ptr<EmojiStatuses> _emojiStatuses;
	const std::unique_ptr<ForumIcons> _forumIcons;
	const std::unique_ptr<NotifySettings> _notifySettings;
	const std::unique_ptr<CustomEmojiManager> _customEmojiManager;
	const std::unique_ptr<Stories> _stories;
	const std::unique_ptr<SavedMessages> _savedMessages;
	const std::unique_ptr<Chatbots> _chatbots;
	const std::unique_ptr<BusinessInfo> _businessInfo;
	std::unique_ptr<ShortcutMessages> _shortcutMessages;

	MsgId _nonHistoryEntryId = ShortcutMaxMsgId;

	rpl::lifetime _lifetime;

};

} // namespace Data
