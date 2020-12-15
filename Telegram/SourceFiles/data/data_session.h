/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_databases.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "data/data_groups.h"
#include "data/data_cloud_file.h"
#include "data/data_notify_settings.h"
#include "history/history_location_manager.h"
#include "base/timer.h"
#include "base/flags.h"
#include "ui/effects/animations.h"

class Image;
class HistoryItem;
class HistoryMessage;
class HistoryService;
struct WebPageCollage;
enum class WebPageType;
enum class NewMessageType;

namespace HistoryView {
struct Group;
class Element;
class ElementDelegate;
class SendActionPainter;
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

namespace Data {

class Folder;
class LocationPoint;
class WallPaper;
class ScheduledMessages;
class ChatFilters;
class CloudThemes;
class Streaming;
class MediaRotation;
class Histories;
class DocumentMedia;
class PhotoMedia;
class Stickers;
class GroupCall;

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
	[[nodiscard]] ChatFilters &chatsFilters() const {
		return *_chatsFilters;
	}
	[[nodiscard]] ScheduledMessages &scheduledMessages() const {
		return *_scheduledMessages;
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
	[[nodiscard]] MsgId nextNonHistoryEntryId() {
		return ++_nonHistoryEntryId;
	}

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
	GroupCall *groupCall(uint64 callId) const;

	struct GroupCallDiscard {
		uint64 id = 0;
		int duration = 0;
	};
	rpl::producer<GroupCallDiscard> groupCallDiscards() const;
	void groupCallDiscarded(uint64 id, int duration);

	[[nodiscard]] auto invitedToCallUsers(uint64 callId) const
		-> const base::flat_set<not_null<UserData*>> &;
	void registerInvitedToCallUser(
		uint64 callId,
		not_null<PeerData*> peer,
		not_null<UserData*> user);
	void unregisterInvitedToCallUser(uint64 callId, not_null<UserData*> user);

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

	void cancelForwarding(not_null<History*> history);

	void registerSendAction(
		not_null<History*> history,
		MsgId rootId,
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

	void userIsBotChanged(not_null<UserData*> user);
	[[nodiscard]] rpl::producer<not_null<UserData*>> userIsBotChanges() const;
	void botCommandsChanged(not_null<UserData*> user);
	[[nodiscard]] rpl::producer<not_null<UserData*>> botCommandsChanges() const;

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
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRemoved(
		FullMsgId itemId) const;
	void notifyViewRemoved(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRemoved() const;
	void notifyHistoryCleared(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyCleared() const;
	void notifyHistoryChangeDelayed(not_null<History*> history);
	[[nodiscard]] rpl::producer<not_null<History*>> historyChanged() const;
	void sendHistoryChangeNotifications();

	void notifyPinnedDialogsOrderUpdated();
	[[nodiscard]] rpl::producer<> pinnedDialogsOrderUpdated() const;

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

	int pinnedChatsCount(Data::Folder *folder, FilterId filterId) const;
	int pinnedChatsLimit(Data::Folder *folder, FilterId filterId) const;
	const std::vector<Dialogs::Key> &pinnedChatsOrder(
		Data::Folder *folder,
		FilterId filterId) const;
	void setChatPinned(
		const Dialogs::Key &key,
		FilterId filterId,
		bool pinned);
	void clearPinnedChats(Data::Folder *folder);
	void applyPinnedChats(
		Data::Folder *folder,
		const QVector<MTPDialogPeer> &list);
	void reorderTwoPinnedChats(
		FilterId filterId,
		const Dialogs::Key &key1,
		const Dialogs::Key &key2);

	void registerMessage(not_null<HistoryItem*> item);
	void unregisterMessage(not_null<HistoryItem*> item);

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
	[[nodiscard]] auto speakingAnimationUpdated() const
		-> rpl::producer<not_null<History*>>;
	void updateSpeakingAnimation(not_null<History*> history);

	using SendActionPainter = HistoryView::SendActionPainter;
	[[nodiscard]] std::shared_ptr<SendActionPainter> repliesSendActionPainter(
		not_null<History*> history,
		MsgId rootId);
	void repliesSendActionPainterRemoved(
		not_null<History*> history,
		MsgId rootId);
	void repliesSendActionPaintersClear(
		not_null<History*> history,
		not_null<UserData*> user);

	[[nodiscard]] int unreadBadge() const;
	[[nodiscard]] bool unreadBadgeMuted() const;
	[[nodiscard]] int unreadBadgeIgnoreOne(const Dialogs::Key &key) const;
	[[nodiscard]] bool unreadBadgeMutedIgnoreOne(const Dialogs::Key &key) const;
	[[nodiscard]] int unreadOnlyMutedBadge() const;
	[[nodiscard]] rpl::producer<> unreadBadgeChanges() const;
	void notifyUnreadBadgeChanged();

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
		const ImageWithLocation &video,
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
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		int32 dc,
		int32 size);
	void documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data);
	[[nodiscard]] DocumentData *documentFromWeb(
		const MTPWebDocument &data,
		const ImageLocation &thumbnailLocation,
		const ImageLocation &videoThumbnailLocation);

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

	[[nodiscard]] not_null<Data::CloudImage*> location(
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
	//void setDefaultFeedId(FeedId id); // #feed
	//FeedId defaultFeedId() const;
	//rpl::producer<FeedId> defaultFeedIdValue() const;

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList(
		Data::Folder *folder = nullptr);
	[[nodiscard]] not_null<const Dialogs::MainList*> chatsList(
		Data::Folder *folder = nullptr) const;
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

	void requestNotifySettings(not_null<PeerData*> peer);
	void applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings);
	void updateNotifySettings(
		not_null<PeerData*> peer,
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts = std::nullopt);
	void resetNotifySettingsToDefault(not_null<PeerData*> peer);
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

	void setMimeForwardIds(MessageIdsList &&list);
	MessageIdsList takeMimeForwardIds();

	void setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message);

	bool updateWallpapers(const MTPaccount_WallPapers &data);
	void removeWallpaper(const WallPaper &paper);
	const std::vector<WallPaper> &wallpapers() const;
	int32 wallpapersHash() const;

	void clearLocalStorage();

private:
	using Messages = std::unordered_map<MsgId, not_null<HistoryItem*>>;

	void suggestStartExport();

	void setupMigrationViewer();
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
	not_null<HistoryItem*> registerMessage(
		std::unique_ptr<HistoryItem> item);
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
		bool hasStickers,
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &video,
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
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &videoThumbnail,
		int32 dc,
		int32 size);
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
	[[nodiscard]] SendActionPainter *lookupSendActionPainter(
		not_null<History*> history,
		MsgId rootId);

	void setWallpapers(const QVector<MTPWallPaper> &data, int32 hash);

	void checkPollsClosings();

	const not_null<Main::Session*> _session;

	Storage::DatabasePointer _cache;
	Storage::DatabasePointer _bigFileCache;

	TimeId _exportAvailableAt = 0;
	QPointer<Ui::BoxContent> _exportSuggestion;

	rpl::variable<bool> _contactsLoaded = false;
	rpl::event_stream<Data::Folder*> _chatsListLoadedEvents;
	rpl::event_stream<Data::Folder*> _chatsListChanged;
	rpl::event_stream<not_null<UserData*>> _userIsBotChanges;
	rpl::event_stream<not_null<UserData*>> _botCommandsChanges;
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
	rpl::event_stream<ChatListEntryRefresh> _chatListEntryRefreshes;
	rpl::event_stream<> _unreadBadgeChanges;

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
	base::flat_map<
		std::pair<not_null<History*>, MsgId>,
		crl::time> _sendActions;
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
		std::unique_ptr<Data::CloudImage>> _locations;
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

	base::flat_set<not_null<WebPageData*>> _webpagesUpdated;
	base::flat_set<not_null<GameData*>> _gamesUpdated;
	base::flat_set<not_null<PollData*>> _pollsUpdated;

	rpl::event_stream<not_null<WebPageData*>> _webpageUpdates;
	rpl::event_stream<not_null<ChannelData*>> _channelDifferenceTooLong;

	base::flat_multi_map<TimeId, not_null<PollData*>> _pollsClosings;
	base::Timer _pollsClosingTimer;

	base::flat_map<FolderId, std::unique_ptr<Folder>> _folders;
	//rpl::variable<FeedId> _defaultFeedId = FeedId(); // #feed

	std::unordered_map<
		not_null<const HistoryItem*>,
		std::vector<not_null<ViewElement*>>> _views;

	rpl::event_stream<> _pinnedDialogsOrderUpdated;

	base::flat_set<not_null<ViewElement*>> _heavyViewParts;

	base::flat_map<uint64, not_null<GroupCall*>> _groupCalls;
	base::flat_map<uint64, base::flat_set<not_null<UserData*>>> _invitedToCallUsers;
	rpl::event_stream<GroupCallDiscard> _groupCallDiscarded;

	History *_topPromoted = nullptr;

	NotifySettings _defaultUserNotifySettings;
	NotifySettings _defaultChatNotifySettings;
	NotifySettings _defaultBroadcastNotifySettings;
	rpl::event_stream<> _defaultUserNotifyUpdates;
	rpl::event_stream<> _defaultChatNotifyUpdates;
	rpl::event_stream<> _defaultBroadcastNotifyUpdates;
	std::unordered_set<not_null<const PeerData*>> _mutedPeers;
	base::Timer _unmuteByFinishedTimer;

	std::unordered_map<PeerId, std::unique_ptr<PeerData>> _peers;

	MessageIdsList _mimeForwardIds;

	using CredentialsWithGeneration = std::pair<
		const Passport::SavedCredentials,
		int>;
	std::unique_ptr<CredentialsWithGeneration> _passportCredentials;

	rpl::event_stream<SendActionAnimationUpdate> _sendActionAnimationUpdate;
	rpl::event_stream<not_null<History*>> _speakingAnimationUpdate;

	std::vector<WallPaper> _wallpapers;
	int32 _wallpapersHash = 0;

	Groups _groups;
	std::unique_ptr<ChatFilters> _chatsFilters;
	std::unique_ptr<ScheduledMessages> _scheduledMessages;
	std::unique_ptr<CloudThemes> _cloudThemes;
	std::unique_ptr<Streaming> _streaming;
	std::unique_ptr<MediaRotation> _mediaRotation;
	std::unique_ptr<Histories> _histories;
	base::flat_map<
		not_null<History*>,
		base::flat_map<
			MsgId,
			std::weak_ptr<SendActionPainter>>> _sendActionPainters;
	std::unique_ptr<Stickers> _stickers;
	MsgId _nonHistoryEntryId = ServerMaxMsgId;

	rpl::lifetime _lifetime;

};

} // namespace Data
