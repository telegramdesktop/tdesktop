/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_databases.h"
#include "chat_helpers/stickers.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "data/data_groups.h"
#include "data/data_notify_settings.h"
#include "history/history_location_manager.h"
#include "base/timer.h"
#include "base/flags.h"
#include "ui/effects/animations.h"

class Image;
class HistoryItem;
class HistoryMessage;
class HistoryService;
class BoxContent;
struct WebPageCollage;
enum class WebPageType;
enum class NewMessageType;

namespace HistoryView {
struct Group;
class Element;
class ElementDelegate;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Media {
namespace Clip {
class Reader;
} // namespace Clip
namespace Streaming {
class Reader;
} // namespace Streaming
} // namespace Media

namespace Export {
class Controller;
namespace View {
class PanelController;
} // namespace View
} // namespace Export

namespace Passport {
struct SavedCredentials;
} // namespace Passport

namespace Data {

class Folder;
class LocationPoint;
class WallPaper;
class ScheduledMessages;

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

	[[nodiscard]] Groups &groups() {
		return _groups;
	}
	[[nodiscard]] const Groups &groups() const {
		return _groups;
	}

	[[nodiscard]] ScheduledMessages &scheduledMessages() const {
		return *_scheduledMessages;
	}
	[[nodiscard]] MsgId nextNonHistoryEntryId() {
		return ++_nonHistoryEntryId;
	}

	void clear();

	void startExport(PeerData *peer = nullptr);
	void startExport(const MTPInputPeer &singlePeer);
	void suggestStartExport(TimeId availableAt);
	void clearExportSuggestion();
	[[nodiscard]] auto currentExportView() const
	-> rpl::producer<Export::View::PanelController*>;
	bool exportInProgress() const;
	void stopExportWithConfirmation(FnMut<void()> callback);
	void stopExport();

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

	void enumerateUsers(Fn<void(not_null<UserData*>)> action) const;
	void enumerateGroups(Fn<void(not_null<PeerData*>)> action) const;
	void enumerateChannels(Fn<void(not_null<ChannelData*>)> action) const;
	[[nodiscard]] PeerData *peerByUsername(const QString &username) const;

	[[nodiscard]] not_null<History*> history(PeerId peerId);
	[[nodiscard]] History *historyLoaded(PeerId peerId) const;
	[[nodiscard]] not_null<History*> history(UserId userId) = delete;
	[[nodiscard]] History *historyLoaded(UserId userId) const = delete;
	[[nodiscard]] not_null<History*> history(not_null<const PeerData*> peer);
	[[nodiscard]] History *historyLoaded(const PeerData *peer);

	void deleteConversationLocally(not_null<PeerData*> peer);

	void registerSendAction(
		not_null<History*> history,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when);

	[[nodiscard]] rpl::variable<bool> &contactsLoaded() {
		return _contactsLoaded;
	}
	[[nodiscard]] rpl::producer<Data::Folder*> chatsListChanges() const {
		return _chatsListChanged.events();
	}
	[[nodiscard]] bool chatsListLoaded(Data::Folder *folder = nullptr);
	[[nodiscard]] rpl::producer<Data::Folder*> chatsListLoadedEvents() const {
		return _chatsListLoadedEvents.events();
	}
	void chatsListChanged(FolderId folderId);
	void chatsListChanged(Data::Folder *folder);
	void chatsListDone(Data::Folder *folder);

	struct ItemVisibilityQuery {
		not_null<HistoryItem*> item;
		not_null<bool*> isVisible;
	};
	[[nodiscard]] base::Observable<ItemVisibilityQuery> &queryItemVisibility() {
		return _queryItemVisibility;
	}
	struct IdChange {
		not_null<HistoryItem*> item;
		MsgId oldId = 0;
	};
	void notifyItemIdChange(IdChange event);
	[[nodiscard]] rpl::producer<IdChange> itemIdChanged() const;
	void notifyItemLayoutChange(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemLayoutChanged() const;
	void notifyViewLayoutChange(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewLayoutChanged() const;
	void notifyUnreadItemAdded(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> unreadItemAdded() const;
	void requestItemRepaint(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRepaintRequest() const;
	void requestViewRepaint(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRepaintRequest() const;
	void requestItemResize(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemResizeRequest() const;
	void requestViewResize(not_null<ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<ViewElement*>> viewResizeRequest() const;
	void requestItemViewRefresh(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> itemViewRefreshRequest() const;
	void requestItemTextRefresh(not_null<HistoryItem*> item);
	void requestAnimationPlayInline(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> animationPlayInlineRequest() const;
	void notifyHistoryUnloaded(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyUnloaded() const;

	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRemoved() const;
	void notifyViewRemoved(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRemoved() const;
	void notifyHistoryCleared(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyCleared() const;
	void notifyHistoryChangeDelayed(not_null<History*> history);
	[[nodiscard]] rpl::producer<not_null<History*>> historyChanged() const;
	void sendHistoryChangeNotifications();

	void registerHeavyViewPart(not_null<ViewElement*> view);
	void unregisterHeavyViewPart(not_null<ViewElement*> view);
	void unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate);
	void unloadHeavyViewParts(
		not_null<HistoryView::ElementDelegate*> delegate,
		int from,
		int till);

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

	void notifyStickersUpdated();
	[[nodiscard]] rpl::producer<> stickersUpdated() const;
	void notifyRecentStickersUpdated();
	[[nodiscard]] rpl::producer<> recentStickersUpdated() const;
	void notifySavedGifsUpdated();
	[[nodiscard]] rpl::producer<> savedGifsUpdated() const;
	void notifyPinnedDialogsOrderUpdated();
	[[nodiscard]] rpl::producer<> pinnedDialogsOrderUpdated() const;

	bool stickersUpdateNeeded(crl::time now) const {
		return stickersUpdateNeeded(_lastStickersUpdate, now);
	}
	void setLastStickersUpdate(crl::time update) {
		_lastStickersUpdate = update;
	}
	bool recentStickersUpdateNeeded(crl::time now) const {
		return stickersUpdateNeeded(_lastRecentStickersUpdate, now);
	}
	void setLastRecentStickersUpdate(crl::time update) {
		if (update) {
			notifyRecentStickersUpdated();
		}
		_lastRecentStickersUpdate = update;
	}
	bool favedStickersUpdateNeeded(crl::time now) const {
		return stickersUpdateNeeded(_lastFavedStickersUpdate, now);
	}
	void setLastFavedStickersUpdate(crl::time update) {
		_lastFavedStickersUpdate = update;
	}
	bool featuredStickersUpdateNeeded(crl::time now) const {
		return stickersUpdateNeeded(_lastFeaturedStickersUpdate, now);
	}
	void setLastFeaturedStickersUpdate(crl::time update) {
		_lastFeaturedStickersUpdate = update;
	}
	bool savedGifsUpdateNeeded(crl::time now) const {
		return stickersUpdateNeeded(_lastSavedGifsUpdate, now);
	}
	void setLastSavedGifsUpdate(crl::time update) {
		_lastSavedGifsUpdate = update;
	}
	int featuredStickerSetsUnreadCount() const {
		return _featuredStickerSetsUnreadCount.current();
	}
	void setFeaturedStickerSetsUnreadCount(int count) {
		_featuredStickerSetsUnreadCount = count;
	}
	[[nodiscard]] rpl::producer<int> featuredStickerSetsUnreadCountValue() const {
		return _featuredStickerSetsUnreadCount.value();
	}
	const Stickers::Sets &stickerSets() const {
		return _stickerSets;
	}
	Stickers::Sets &stickerSetsRef() {
		return _stickerSets;
	}
	const Stickers::Order &stickerSetsOrder() const {
		return _stickerSetsOrder;
	}
	Stickers::Order &stickerSetsOrderRef() {
		return _stickerSetsOrder;
	}
	const Stickers::Order &featuredStickerSetsOrder() const {
		return _featuredStickerSetsOrder;
	}
	Stickers::Order &featuredStickerSetsOrderRef() {
		return _featuredStickerSetsOrder;
	}
	const Stickers::Order &archivedStickerSetsOrder() const {
		return _archivedStickerSetsOrder;
	}
	Stickers::Order &archivedStickerSetsOrderRef() {
		return _archivedStickerSetsOrder;
	}
	const Stickers::SavedGifs &savedGifs() const {
		return _savedGifs;
	}
	Stickers::SavedGifs &savedGifsRef() {
		return _savedGifs;
	}

	void addSavedGif(not_null<DocumentData*> document);
	void checkSavedGif(not_null<HistoryItem*> item);

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
		Data::Folder *requestFolder,
		const QVector<MTPMessage> &messages,
		const QVector<MTPDialog> &dialogs,
		std::optional<int> count = std::nullopt);

	int pinnedChatsCount(Data::Folder *folder) const;
	int pinnedChatsLimit(Data::Folder *folder) const;
	const std::vector<Dialogs::Key> &pinnedChatsOrder(
		Data::Folder *folder) const;
	void setChatPinned(const Dialogs::Key &key, bool pinned);
	void clearPinnedChats(Data::Folder *folder);
	void applyPinnedChats(
		Data::Folder *folder,
		const QVector<MTPDialogPeer> &list);
	void reorderTwoPinnedChats(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2);

	template <typename ...Args>
	not_null<HistoryMessage*> makeMessage(Args &&...args) {
		return static_cast<HistoryMessage*>(
			registerMessage(
				std::make_unique<HistoryMessage>(
					std::forward<Args>(args)...)));
	}

	template <typename ...Args>
	not_null<HistoryService*> makeServiceMessage(Args &&...args) {
		return static_cast<HistoryService*>(
			registerMessage(
				std::make_unique<HistoryService>(
					std::forward<Args>(args)...)));
	}
	void destroyMessage(not_null<HistoryItem*> item);

	// Returns true if item found and it is not detached.
	bool checkEntitiesAndViewsUpdate(const MTPDmessage &data);
	void updateEditedMessage(const MTPMessage &data);
	void processMessages(
		const QVector<MTPMessage> &data,
		NewMessageType type);
	void processMessages(
		const MTPVector<MTPMessage> &data,
		NewMessageType type);
	void processMessagesDeleted(
		ChannelId channelId,
		const QVector<MTPint> &data);

	[[nodiscard]] MsgId nextLocalMessageId();
	[[nodiscard]] HistoryItem *message(
		ChannelId channelId,
		MsgId itemId) const;
	[[nodiscard]] HistoryItem *message(
		const ChannelData *channel,
		MsgId itemId) const;
	[[nodiscard]] HistoryItem *message(FullMsgId itemId) const;

	void updateDependentMessages(not_null<HistoryItem*> item);
	void registerDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency);
	void unregisterDependentMessage(
		not_null<HistoryItem*> dependent,
		not_null<HistoryItem*> dependency);

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
	void notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document);
	void requestDocumentViewRepaint(not_null<const DocumentData*> document);
	void markMediaRead(not_null<const DocumentData*> document);
	void requestPollViewRepaint(not_null<const PollData*> poll);

	std::shared_ptr<::Media::Streaming::Reader> documentStreamedReader(
		not_null<DocumentData*> document,
		FileOrigin origin,
		bool forceRemoteLoader = false);

	HistoryItem *addNewMessage(
		const MTPMessage &data,
		MTPDmessage_ClientFlags flags,
		NewMessageType type);

	struct SendActionAnimationUpdate {
		not_null<History*> history;
		int width = 0;
		int height = 0;
		bool textUpdated = false;
	};
	[[nodiscard]] auto sendActionAnimationUpdated() const
		-> rpl::producer<SendActionAnimationUpdate>;
	void updateSendActionAnimation(SendActionAnimationUpdate &&update);

	int unreadBadge() const;
	bool unreadBadgeMuted() const;
	int unreadBadgeIgnoreOne(const Dialogs::Key &key) const;
	bool unreadBadgeMutedIgnoreOne(const Dialogs::Key &key) const;
	int unreadOnlyMutedBadge() const;

	void unreadStateChanged(
		const Dialogs::Key &key,
		const Dialogs::UnreadState &wasState);
	void unreadEntryChanged(const Dialogs::Key &key, bool added);

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
		bool hasSticker,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnailSmall,
		const ImagePtr &thumbnail,
		const ImagePtr &large);
	void photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data);
	[[nodiscard]] PhotoData *photoFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumbnail = ImagePtr(),
		bool willBecomeNormal = false);

	[[nodiscard]] not_null<DocumentData*> document(DocumentId id);
	not_null<DocumentData*> processDocument(const MTPDocument &data);
	not_null<DocumentData*> processDocument(const MTPDdocument &data);
	not_null<DocumentData*> processDocument(
		const MTPdocument &data,
		QImage &&thumb);
	[[nodiscard]] not_null<DocumentData*> document(
		DocumentId id,
		const uint64 &access,
		const QByteArray &fileReference,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnail,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation);
	void documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data);
	[[nodiscard]] DocumentData *documentFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumb);

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
		int duration,
		const QString &author,
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

	[[nodiscard]] not_null<PollData*> poll(PollId id);
	not_null<PollData*> processPoll(const MTPPoll &data);
	not_null<PollData*> processPoll(const MTPDmessageMediaPoll &data);

	[[nodiscard]] not_null<LocationThumbnail*> location(
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
	void registerAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader,
		not_null<ViewElement*> view);
	void unregisterAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader);

	HistoryItem *findWebPageItem(not_null<WebPageData*> page) const;
	QString findContactPhone(not_null<UserData*> contact) const;
	QString findContactPhone(UserId contactId) const;

	void notifyWebPageUpdateDelayed(not_null<WebPageData*> page);
	void notifyGameUpdateDelayed(not_null<GameData*> game);
	void notifyPollUpdateDelayed(not_null<PollData*> poll);
	bool hasPendingWebPageGamePollNotification() const;
	void sendWebPageGamePollNotifications();

	void stopAutoplayAnimations();

	void registerItemView(not_null<ViewElement*> view);
	void unregisterItemView(not_null<ViewElement*> view);

	[[nodiscard]] not_null<Folder*> folder(FolderId id);
	[[nodiscard]] Folder *folderLoaded(FolderId id) const;
	not_null<Folder*> processFolder(const MTPFolder &data);
	not_null<Folder*> processFolder(const MTPDfolder &data);
	//void setDefaultFeedId(FeedId id); // #feed
	//FeedId defaultFeedId() const;
	//rpl::producer<FeedId> defaultFeedIdValue() const;

	not_null<Dialogs::MainList*> chatsList(Data::Folder *folder = nullptr);
	not_null<const Dialogs::MainList*> chatsList(
		Data::Folder *folder = nullptr) const;
	not_null<Dialogs::IndexedList*> contactsList();
	not_null<Dialogs::IndexedList*> contactsNoChatsList();

	struct RefreshChatListEntryResult {
		bool changed = false;
		bool importantChanged = false;
		Dialogs::PositionChange moved;
		Dialogs::PositionChange importantMoved;
	};
	RefreshChatListEntryResult refreshChatListEntry(Dialogs::Key key);
	void removeChatListEntry(Dialogs::Key key);

	struct DialogsRowReplacement {
		not_null<Dialogs::Row*> old;
		Dialogs::Row *now = nullptr;
	};
	void dialogsRowReplaced(DialogsRowReplacement replacement);
	rpl::producer<DialogsRowReplacement> dialogsRowReplacements() const;

	void requestNotifySettings(not_null<PeerData*> peer);
	void applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings);
	void updateNotifySettings(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts = std::nullopt);
	bool notifyIsMuted(
		not_null<const PeerData*> peer,
		crl::time *changesIn = nullptr) const;
	bool notifySilentPosts(not_null<const PeerData*> peer) const;
	bool notifyMuteUnknown(not_null<const PeerData*> peer) const;
	bool notifySilentPostsUnknown(not_null<const PeerData*> peer) const;
	bool notifySettingsUnknown(not_null<const PeerData*> peer) const;
	rpl::producer<> defaultUserNotifyUpdates() const;
	rpl::producer<> defaultChatNotifyUpdates() const;
	rpl::producer<> defaultBroadcastNotifyUpdates() const;
	rpl::producer<> defaultNotifyUpdates(
		not_null<const PeerData*> peer) const;

	void serviceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media = MTP_messageMediaEmpty());
	void checkNewAuthorization();
	rpl::producer<> newAuthorizationChecks() const;

	void setMimeForwardIds(MessageIdsList &&list);
	MessageIdsList takeMimeForwardIds();

	void setProxyPromoted(PeerData *promoted);
	PeerData *proxyPromoted() const;

	bool updateWallpapers(const MTPaccount_WallPapers &data);
	void removeWallpaper(const WallPaper &paper);
	const std::vector<WallPaper> &wallpapers() const;
	int32 wallpapersHash() const;

	void clearLocalStorage();

private:
	using Messages = std::unordered_map<MsgId, std::unique_ptr<HistoryItem>>;

	void suggestStartExport();

	void setupContactViewsViewer();
	void setupChannelLeavingViewer();
	void setupPeerNameViewer();
	void setupUserIsContactViewer();

	void checkSelfDestructItems();

	int computeUnreadBadge(const Dialogs::UnreadState &state) const;
	bool computeUnreadBadgeMuted(const Dialogs::UnreadState &state) const;

	void applyDialog(Data::Folder *requestFolder, const MTPDdialog &data);
	void applyDialog(
		Data::Folder *requestFolder,
		const MTPDdialogFolder &data);

	const Messages *messagesList(ChannelId channelId) const;
	not_null<Messages*> messagesListForInsert(ChannelId channelId);
	HistoryItem *registerMessage(std::unique_ptr<HistoryItem> item);
	void changeMessageId(ChannelId channel, MsgId wasId, MsgId nowId);
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
		bool hasSticker,
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnailSmall,
		const ImagePtr &thumbnail,
		const ImagePtr &large);

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
		const ImagePtr &thumbnailInline,
		const ImagePtr &thumbnail,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation);
	DocumentData *documentFromWeb(
		const MTPDwebDocument &data,
		ImagePtr thumb);
	DocumentData *documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		ImagePtr thumb);

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
		PhotoData *photo,
		DocumentData *document,
		WebPageCollage &&collage,
		int duration,
		const QString &author,
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

	void folderApplyFields(
		not_null<Folder*> folder,
		const MTPDfolder &data);

	bool stickersUpdateNeeded(crl::time lastUpdate, crl::time now) const {
		constexpr auto kStickersUpdateTimeout = crl::time(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kStickersUpdateTimeout);
	}
	void userIsContactUpdated(not_null<UserData*> user);

	void setPinnedFromDialog(const Dialogs::Key &key, bool pinned);

	NotifySettings &defaultNotifySettings(not_null<const PeerData*> peer);
	const NotifySettings &defaultNotifySettings(
		not_null<const PeerData*> peer) const;
	void unmuteByFinished();
	void unmuteByFinishedDelayed(crl::time delay);
	void updateNotifySettingsLocal(not_null<PeerData*> peer);

	template <typename Method>
	void enumerateItemViews(
		not_null<const HistoryItem*> item,
		Method method);

	void insertCheckedServiceNotification(
		const TextWithEntities &message,
		const MTPMessageMedia &media,
		TimeId date);

	bool sendActionsAnimationCallback(crl::time now);

	void setWallpapers(const QVector<MTPWallPaper> &data, int32 hash);

	not_null<Main::Session*> _session;

	Storage::DatabasePointer _cache;
	Storage::DatabasePointer _bigFileCache;

	std::unique_ptr<Export::Controller> _export;
	std::unique_ptr<Export::View::PanelController> _exportPanel;
	rpl::event_stream<Export::View::PanelController*> _exportViewChanges;
	TimeId _exportAvailableAt = 0;
	QPointer<BoxContent> _exportSuggestion;

	rpl::variable<bool> _contactsLoaded = false;
	rpl::event_stream<Data::Folder*> _chatsListLoadedEvents;
	rpl::event_stream<Data::Folder*> _chatsListChanged;
	base::Observable<ItemVisibilityQuery> _queryItemVisibility;
	rpl::event_stream<IdChange> _itemIdChanges;
	rpl::event_stream<not_null<const HistoryItem*>> _itemLayoutChanges;
	rpl::event_stream<not_null<const ViewElement*>> _viewLayoutChanges;
	rpl::event_stream<not_null<HistoryItem*>> _unreadItemAdded;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRepaintRequest;
	rpl::event_stream<not_null<const ViewElement*>> _viewRepaintRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemResizeRequest;
	rpl::event_stream<not_null<ViewElement*>> _viewResizeRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemViewRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemTextRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _animationPlayInlineRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRemoved;
	rpl::event_stream<not_null<const ViewElement*>> _viewRemoved;
	rpl::event_stream<not_null<const History*>> _historyUnloaded;
	rpl::event_stream<not_null<const History*>> _historyCleared;
	base::flat_set<not_null<History*>> _historiesChanged;
	rpl::event_stream<not_null<History*>> _historyChanged;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantRemoved;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantAdded;
	rpl::event_stream<DialogsRowReplacement> _dialogsRowReplacements;

	rpl::event_stream<> _stickersUpdated;
	rpl::event_stream<> _recentStickersUpdated;
	rpl::event_stream<> _savedGifsUpdated;
	rpl::event_stream<> _pinnedDialogsOrderUpdated;
	crl::time _lastStickersUpdate = 0;
	crl::time _lastRecentStickersUpdate = 0;
	crl::time _lastFavedStickersUpdate = 0;
	crl::time _lastFeaturedStickersUpdate = 0;
	crl::time _lastSavedGifsUpdate = 0;
	rpl::variable<int> _featuredStickerSetsUnreadCount = 0;
	Stickers::Sets _stickerSets;
	Stickers::Order _stickerSetsOrder;
	Stickers::Order _featuredStickerSetsOrder;
	Stickers::Order _archivedStickerSetsOrder;
	Stickers::SavedGifs _savedGifs;

	Dialogs::MainList _chatsList;
	Dialogs::IndexedList _contactsList;
	Dialogs::IndexedList _contactsNoChatsList;

	MsgId _localMessageIdCounter = StartClientMsgId;
	Messages _messages;
	std::map<ChannelId, Messages> _channelMessages;
	std::map<
		not_null<HistoryItem*>,
		base::flat_set<not_null<HistoryItem*>>> _dependentMessages;

	base::flat_map<uint64, FullMsgId> _messageByRandomId;
	base::flat_map<uint64, SentData> _sentMessagesData;

	base::Timer _selfDestructTimer;
	std::vector<FullMsgId> _selfDestructItems;

	// When typing in this history started.
	base::flat_map<not_null<History*>, crl::time> _sendActions;
	Ui::Animations::Basic _sendActionsAnimation;

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
		std::unique_ptr<LocationThumbnail>> _locations;
	std::unordered_map<
		PollId,
		std::unique_ptr<PollData>> _polls;
	std::unordered_map<
		GameId,
		std::unique_ptr<GameData>> _games;
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
	base::flat_map<
		not_null<::Media::Clip::Reader*>,
		not_null<ViewElement*>> _autoplayAnimations;

	base::flat_set<not_null<WebPageData*>> _webpagesUpdated;
	base::flat_set<not_null<GameData*>> _gamesUpdated;
	base::flat_set<not_null<PollData*>> _pollsUpdated;

	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<::Media::Streaming::Reader>> _streamedReaders;

	base::flat_map<FolderId, std::unique_ptr<Folder>> _folders;
	//rpl::variable<FeedId> _defaultFeedId = FeedId(); // #feed

	std::unordered_map<
		not_null<const HistoryItem*>,
		std::vector<not_null<ViewElement*>>> _views;

	base::flat_set<not_null<ViewElement*>> _heavyViewParts;

	PeerData *_proxyPromoted = nullptr;

	NotifySettings _defaultUserNotifySettings;
	NotifySettings _defaultChatNotifySettings;
	NotifySettings _defaultBroadcastNotifySettings;
	rpl::event_stream<> _defaultUserNotifyUpdates;
	rpl::event_stream<> _defaultChatNotifyUpdates;
	rpl::event_stream<> _defaultBroadcastNotifyUpdates;
	std::unordered_set<not_null<const PeerData*>> _mutedPeers;
	base::Timer _unmuteByFinishedTimer;

	std::unordered_map<PeerId, std::unique_ptr<PeerData>> _peers;
	std::unordered_map<PeerId, std::unique_ptr<History>> _histories;

	MessageIdsList _mimeForwardIds;

	using CredentialsWithGeneration = std::pair<
		const Passport::SavedCredentials,
		int>;
	std::unique_ptr<CredentialsWithGeneration> _passportCredentials;

	rpl::event_stream<> _newAuthorizationChecks;

	rpl::event_stream<SendActionAnimationUpdate> _sendActionAnimationUpdate;

	std::vector<WallPaper> _wallpapers;
	int32 _wallpapersHash = 0;

	Groups _groups;
	std::unique_ptr<ScheduledMessages> _scheduledMessages;
	MsgId _nonHistoryEntryId = ServerMaxMsgId;

	rpl::lifetime _lifetime;

};

} // namespace Data
