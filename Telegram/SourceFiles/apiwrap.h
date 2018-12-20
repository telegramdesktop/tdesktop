/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "base/timer.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "core/single_timer.h"
#include "mtproto/sender.h"
#include "chat_helpers/stickers.h"
#include "data/data_messages.h"

class TaskQueue;
class AuthSession;
struct MessageGroupId;
struct SendingAlbum;
enum class SendMediaType;
struct FileLoadTo;
class mtpFileLoader;

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Storage {
enum class SharedMediaType : signed char;
struct PreparedList;
} // namespace Storage

namespace Dialogs {
class Key;
} // namespace Dialogs

namespace Core {
struct CloudPasswordState;
} // namespace Core

namespace Api {

inline const MTPVector<MTPChat> *getChatsFromMessagesChats(const MTPmessages_Chats &chats) {
	switch (chats.type()) {
	case mtpc_messages_chats: return &chats.c_messages_chats().vchats;
	case mtpc_messages_chatsSlice: return &chats.c_messages_chatsSlice().vchats;
	}
	return nullptr;
}

template <typename IntRange>
inline int32 CountHash(IntRange &&range) {
	uint32 acc = 0;
	for (auto value : range) {
		acc += (acc * 20261) + uint32(value);
	}
	return int32(acc & 0x7FFFFFFF);
}

} // namespace Api

class ApiWrap : private MTP::Sender, private base::Subscriber {
public:
	ApiWrap(not_null<AuthSession*> session);

	void applyUpdates(const MTPUpdates &updates, uint64 sentMessageRandomId = 0);

	void savePinnedOrder();
	//void toggleChannelGrouping( // #feed
	//	not_null<ChannelData*> channel,
	//	bool group,
	//	Fn<void()> callback);
	//void ungroupAllFromFeed(not_null<Data::Feed*> feed);

	using RequestMessageDataCallback = Fn<void(ChannelData*, MsgId)>;
	void requestMessageData(
		ChannelData *channel,
		MsgId msgId,
		RequestMessageDataCallback callback);

	void requestContacts();
	void requestDialogEntry(not_null<Data::Feed*> feed);
	//void requestFeedDialogsEntries(not_null<Data::Feed*> feed);
	void requestDialogEntry(
		not_null<History*> history,
		Fn<void()> callback = nullptr);
	void requestDialogEntries(std::vector<not_null<History*>> histories);
	//void applyFeedSources(const MTPDchannels_feedSources &data); // #feed
	//void setFeedChannels(
	//	not_null<Data::Feed*> feed,
	//	const std::vector<not_null<ChannelData*>> &channels);
	void changeDialogUnreadMark(not_null<History*> history, bool unread);
	//void changeDialogUnreadMark(not_null<Data::Feed*> feed, bool unread); // #feed

	void requestFullPeer(PeerData *peer);
	void requestPeer(PeerData *peer);
	void requestPeers(const QList<PeerData*> &peers);
	void requestLastParticipants(not_null<ChannelData*> channel);
	void requestBots(not_null<ChannelData*> channel);
	void requestAdmins(not_null<ChannelData*> channel);
	void requestParticipantsCountDelayed(not_null<ChannelData*> channel);
	void requestChannelRangeDifference(not_null<History*> history);

	using UpdatedFileReferences = Data::UpdatedFileReferences;
	using FileReferencesHandler = FnMut<void(const UpdatedFileReferences&)>;
	void refreshFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler);
	void refreshFileReference(
		Data::FileOrigin origin,
		not_null<mtpFileLoader*> loader,
		int requestId,
		const QByteArray &current);

	void requestChangelog(
		const QString &sinceVersion,
		Fn<void(const MTPUpdates &result)> callback);
	void refreshProxyPromotion();
	void requestDeepLinkInfo(
		const QString &path,
		Fn<void(const MTPDhelp_deepLinkInfo &result)> callback);
	void requestTermsUpdate();
	void acceptTerms(bytes::const_span termsId);

	void checkChatInvite(
		const QString &hash,
		FnMut<void(const MTPChatInvite &)> done,
		FnMut<void(const RPCError &)> fail);
	void importChatInvite(const QString &hash);

	void requestChannelMembersForAdd(
		not_null<ChannelData*> channel,
		Fn<void(const MTPchannels_ChannelParticipants&)> callback);
	void processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result);
	void processFullPeer(UserData *user, const MTPUserFull &result);

	void markMediaRead(const base::flat_set<not_null<HistoryItem*>> &items);
	void markMediaRead(not_null<HistoryItem*> item);

	void requestSelfParticipant(ChannelData *channel);
	void kickParticipant(not_null<ChatData*> chat, not_null<UserData*> user);
	void kickParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChannelBannedRights &currentRights);
	void unblockParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	void deleteAllFromUser(
		not_null<ChannelData*> channel,
		not_null<UserData*> from);

	void requestWebPageDelayed(WebPageData *page);
	void clearWebPageRequest(WebPageData *page);
	void clearWebPageRequests();

	void scheduleStickerSetRequest(uint64 setId, uint64 access);
	void requestStickerSets();
	void saveStickerSets(
		const Stickers::Order &localOrder,
		const Stickers::Order &localRemoved);
	void updateStickers();
	void requestRecentStickersForce();
	void setGroupStickerSet(
		not_null<ChannelData*> megagroup,
		const MTPInputStickerSet &set);
	std::vector<not_null<DocumentData*>> *stickersByEmoji(
		not_null<EmojiPtr> emoji);
	void toggleFavedSticker(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool faved);
	void toggleSavedGif(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool saved);

	void joinChannel(not_null<ChannelData*> channel);
	void leaveChannel(not_null<ChannelData*> channel);

	void blockUser(not_null<UserData*> user);
	void unblockUser(not_null<UserData*> user);

	void exportInviteLink(not_null<PeerData*> peer);
	void requestNotifySettings(const MTPInputNotifyPeer &peer);
	void updateNotifySettingsDelayed(not_null<const PeerData*> peer);
	void saveDraftToCloudDelayed(not_null<History*> history);

	void savePrivacy(const MTPInputPrivacyKey &key, QVector<MTPInputPrivacyRule> &&rules);
	void handlePrivacyChange(mtpTypeId keyTypeId, const MTPVector<MTPPrivacyRule> &rules);
	static int OnlineTillFromStatus(
		const MTPUserStatus &status,
		int currentOnlineTill);

	void clearHistory(not_null<PeerData*> peer);

	base::Observable<PeerData*> &fullPeerUpdated() {
		return _fullPeerUpdated;
	}

	bool isQuitPrevent();

	void applyUpdatesNoPtsCheck(const MTPUpdates &updates);
	void applyUpdateNoPtsCheck(const MTPUpdate &update);

	void jumpToDate(Dialogs::Key chat, const QDate &date);

	void preloadEnoughUnreadMentions(not_null<History*> history);
	void checkForUnreadMentions(const base::flat_set<MsgId> &possiblyReadMentions, ChannelData *channel = nullptr);

	void editChatAdmins(
		not_null<ChatData*> chat,
		bool adminsEnabled,
		base::flat_set<not_null<UserData*>> &&admins);

	using SliceType = Data::LoadDirection;
	void requestSharedMedia(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type,
		MsgId messageId,
		SliceType slice);
	void requestSharedMediaCount(
			not_null<PeerData*> peer,
			Storage::SharedMediaType type);

	void requestUserPhotos(
		not_null<UserData*> user,
		PhotoId afterId);

	//void requestFeedChannels( // #feed
	//	not_null<Data::Feed*> feed);
	//void requestFeedMessages(
	//	not_null<Data::Feed*> feed,
	//	Data::MessagePosition messageId,
	//	SliceType slice);
	//void saveDefaultFeedId(FeedId id, bool isDefaultFeedId);

	void stickerSetInstalled(uint64 setId) {
		_stickerSetInstalled.fire_copy(setId);
	}
	auto stickerSetInstalled() const {
		return _stickerSetInstalled.events();
	}
	void readFeaturedSetDelayed(uint64 setId);

	void parseChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified = nullptr);
	void parseRecentChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified = nullptr);

	struct SendOptions {
		SendOptions(not_null<History*> history);

		not_null<History*> history;
		MsgId replyTo = 0;
		WebPageId webPageId = 0;
		bool clearDraft = false;
		bool generateLocal = true;
		bool handleSupportSwitch = false;
	};
	rpl::producer<SendOptions> sendActions() const {
		return _sendActions.events();
	}
	void sendAction(const SendOptions &options);
	void forwardMessages(
		HistoryItemsList &&items,
		const SendOptions &options,
		FnMut<void()> &&successCallback = nullptr);
	void shareContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		const SendOptions &options);
	void shareContact(not_null<UserData*> user, const SendOptions &options);
	void readServerHistory(not_null<History*> history);
	void readServerHistoryForce(not_null<History*> history);
	void readFeed(
		not_null<Data::Feed*> feed,
		Data::MessagePosition position);

	void sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		int duration,
		const SendOptions &options);
	void sendFiles(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		std::shared_ptr<SendingAlbum> album,
		const SendOptions &options);
	void sendFile(
		const QByteArray &fileContent,
		SendMediaType type,
		const SendOptions &options);

	void sendUploadedPhoto(
		FullMsgId localId,
		const MTPInputFile &file,
		bool silent);
	void sendUploadedDocument(
		FullMsgId localId,
		const MTPInputFile &file,
		const std::optional<MTPInputFile> &thumb,
		bool silent);
	void cancelLocalItem(not_null<HistoryItem*> item);

	struct MessageToSend {
		MessageToSend(not_null<History*> history);

		not_null<History*> history;
		TextWithTags textWithTags;
		MsgId replyTo = 0;
		WebPageId webPageId = 0;
		bool clearDraft = true;
		bool handleSupportSwitch = false;
	};
	void sendMessage(MessageToSend &&message);
	void sendBotStart(not_null<UserData*> bot);
	void sendInlineResult(
		not_null<UserData*> bot,
		not_null<InlineBots::Result*> data,
		const SendOptions &options);
	void sendExistingDocument(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		TextWithEntities caption,
		const SendOptions &options);

	void requestSupportContact(FnMut<void(const MTPUser&)> callback);

	void uploadPeerPhoto(not_null<PeerData*> peer, QImage &&image);
	void clearPeerPhoto(not_null<PhotoData*> photo);

	void reloadPasswordState();
	void clearUnconfirmedPassword();
	rpl::producer<Core::CloudPasswordState> passwordState() const;
	std::optional<Core::CloudPasswordState> passwordStateCurrent() const;

	void saveSelfBio(const QString &text, FnMut<void()> done);

	struct Privacy {
		enum class Key {
			LastSeen,
			Calls,
			Invites,
			CallsPeer2Peer,
		};
		enum class Option {
			Everyone,
			Contacts,
			Nobody,
		};
		Option option = Option::Everyone;
		std::vector<not_null<UserData*>> always;
		std::vector<not_null<UserData*>> never;

		static MTPInputPrivacyKey Input(Key key);
	};
	void reloadPrivacy(Privacy::Key key);
	rpl::producer<Privacy> privacyValue(Privacy::Key key);

	void reloadSelfDestruct();
	rpl::producer<int> selfDestructValue() const;
	void saveSelfDestruct(int days);

	void createPoll(
		const PollData &data,
		const SendOptions &options,
		FnMut<void()> done,
		FnMut<void(const RPCError &error)> fail);
	void sendPollVotes(
		FullMsgId itemId,
		const std::vector<QByteArray> &options);
	void closePoll(FullMsgId itemId);

	~ApiWrap();

private:
	struct MessageDataRequest {
		using Callbacks = QList<RequestMessageDataCallback>;
		mtpRequestId requestId = 0;
		Callbacks callbacks;
	};
	using MessageDataRequests = QMap<MsgId, MessageDataRequest>;
	using SharedMediaType = Storage::SharedMediaType;

	struct StickersByEmoji {
		std::vector<not_null<DocumentData*>> list;
		int32 hash = 0;
		TimeMs received = 0;
	};

	using SimpleFileLocationId = Data::SimpleFileLocationId;
	using DocumentFileLocationId = Data::DocumentFileLocationId;
	using FileLocationId = Data::FileLocationId;

	void updatesReceived(const MTPUpdates &updates);
	void checkQuitPreventFinished();

	void saveDraftsToCloud();

	void resolveMessageDatas();
	void gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId requestId);
	void finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId);

	QVector<MTPInputMessage> collectMessageIds(const MessageDataRequests &requests);
	MessageDataRequests *messageDataRequests(ChannelData *channel, bool onlyExisting = false);
	void applyPeerDialogs(const MTPmessages_PeerDialogs &dialogs);
	void historyDialogEntryApplied(not_null<History*> history);
	void applyFeedDialogs(
		not_null<Data::Feed*> feed,
		const MTPmessages_Dialogs &dialogs);

	void gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req);
	void gotUserFull(UserData *user, const MTPUserFull &result, mtpRequestId req);
	void applyLastParticipantsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list);
	void applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list);
	void applyAdminsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list);
	void resolveWebPages();
	void gotWebPages(
		ChannelData *channel,
		const MTPmessages_Messages &result,
		mtpRequestId req);
	void gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result);

	void channelRangeDifferenceSend(
		not_null<ChannelData*> channel,
		MsgRange range,
		int32 pts);
	void channelRangeDifferenceDone(
		not_null<ChannelData*> channel,
		MsgRange range,
		const MTPupdates_ChannelDifference &result);

	void notifySettingReceived(
		MTPInputNotifyPeer peer,
		const MTPPeerNotifySettings &settings);

	void stickerSetDisenabled(mtpRequestId requestId);
	void stickersSaveOrder();

	void requestStickers(TimeId now);
	void requestRecentStickers(TimeId now);
	void requestRecentStickersWithHash(int32 hash);
	void requestFavedStickers(TimeId now);
	void requestFeaturedStickers(TimeId now);
	void requestSavedGifs(TimeId now);
	void readFeaturedSets();

	void cancelEditChatAdmins(not_null<ChatData*> chat);
	void saveChatAdmins(not_null<ChatData*> chat);
	void sendSaveChatAdminsRequests(not_null<ChatData*> chat);
	void refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants);

	void jumpToHistoryDate(not_null<PeerData*> peer, const QDate &date);
	void jumpToFeedDate(not_null<Data::Feed*> feed, const QDate &date);
	template <typename Callback>
	void requestMessageAfterDate(
		not_null<PeerData*> peer,
		const QDate &date,
		Callback &&callback);
	template <typename Callback>
	void requestMessageAfterDate(
		not_null<Data::Feed*> feed,
		const QDate &date,
		Callback &&callback);

	void sharedMediaDone(
		not_null<PeerData*> peer,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice,
		const MTPmessages_Messages &result);

	void userPhotosDone(
		not_null<UserData*> user,
		PhotoId photoId,
		const MTPphotos_Photos &result);

	//void feedChannelsDone(not_null<Data::Feed*> feed); // #feed
	//void feedMessagesDone(
	//	not_null<Data::Feed*> feed,
	//	Data::MessagePosition messageId,
	//	SliceType slice,
	//	const MTPmessages_FeedMessages &result);

	void sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendOptions &options);

	void sendReadRequest(not_null<PeerData*> peer, MsgId upTo);
	int applyAffectedHistory(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedHistory &result);
	void applyAffectedMessages(const MTPmessages_AffectedMessages &result);
	void applyAffectedMessages(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result);

	void deleteAllFromUserSend(
		not_null<ChannelData*> channel,
		not_null<UserData*> from);

	void sendMessageFail(const RPCError &error);
	void uploadAlbumMedia(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media);
	void sendAlbumWithUploaded(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media);
	void sendAlbumWithCancelled(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId);
	void sendAlbumIfReady(not_null<SendingAlbum*> album);
	void sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		bool silent);
	void sendMediaWithRandomId(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		bool silent,
		uint64 randomId);
	FileLoadTo fileLoadTaskOptions(const SendOptions &options) const;

	void readFeeds();

	void getProxyPromotionDelayed(TimeId now, TimeId next);
	void proxyPromotionDone(const MTPhelp_ProxyData &proxy);

	void sendNotifySettingsUpdates();

	template <typename Request>
	void requestFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler,
		Request &&data);

	void photoUploadReady(const FullMsgId &msgId, const MTPInputFile &file);

	Privacy parsePrivacy(const QVector<MTPPrivacyRule> &rules);
	void pushPrivacy(
		Privacy::Key key,
		const QVector<MTPPrivacyRule> &rules);
	void updatePrivacyLastSeens(const QVector<MTPPrivacyRule> &rules);

	void setSelfDestructDays(int days);

	not_null<AuthSession*> _session;

	MessageDataRequests _messageDataRequests;
	QMap<ChannelData*, MessageDataRequests> _channelMessageDataRequests;
	SingleQueuedInvokation _messageDataResolveDelayed;

	using PeerRequests = QMap<PeerData*, mtpRequestId>;
	PeerRequests _fullPeerRequests;
	PeerRequests _peerRequests;

	PeerRequests _participantsRequests;
	PeerRequests _botsRequests;
	PeerRequests _adminsRequests;
	base::DelayedCallTimer _participantsCountRequestTimer;

	ChannelData *_channelMembersForAdd = nullptr;
	mtpRequestId _channelMembersForAddRequestId = 0;
	Fn<void(
		const MTPchannels_ChannelParticipants&)> _channelMembersForAddCallback;
	base::flat_map<
		not_null<ChannelData*>,
		std::pair<mtpRequestId,Fn<void()>>> _channelGroupingRequests;

	using KickRequest = std::pair<
		not_null<ChannelData*>,
		not_null<UserData*>>;
	base::flat_map<KickRequest, mtpRequestId> _kickRequests;

	QMap<ChannelData*, mtpRequestId> _selfParticipantRequests;

	base::flat_map<
		not_null<ChannelData*>,
		mtpRequestId> _rangeDifferenceRequests;

	QMap<WebPageData*, mtpRequestId> _webPagesPending;
	base::Timer _webPagesTimer;

	QMap<uint64, QPair<uint64, mtpRequestId> > _stickerSetRequests;

	QMap<ChannelData*, mtpRequestId> _channelAmInRequests;
	base::flat_map<not_null<UserData*>, mtpRequestId> _blockRequests;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _exportInviteRequests;
	base::flat_map<PeerId, mtpRequestId> _notifySettingRequests;
	base::flat_map<not_null<History*>, mtpRequestId> _draftsSaveRequestIds;
	base::Timer _draftsSaveTimer;

	base::flat_set<mtpRequestId> _stickerSetDisenableRequests;
	Stickers::Order _stickersOrder;
	mtpRequestId _stickersReorderRequestId = 0;
	mtpRequestId _stickersClearRecentRequestId = 0;

	mtpRequestId _stickersUpdateRequest = 0;
	mtpRequestId _recentStickersUpdateRequest = 0;
	mtpRequestId _favedStickersUpdateRequest = 0;
	mtpRequestId _featuredStickersUpdateRequest = 0;
	mtpRequestId _savedGifsUpdateRequest = 0;

	base::Timer _featuredSetsReadTimer;
	base::flat_set<uint64> _featuredSetsRead;

	base::flat_map<not_null<EmojiPtr>, StickersByEmoji> _stickersByEmoji;

	base::flat_map<mtpTypeId, mtpRequestId> _privacySaveRequests;

	mtpRequestId _contactsRequestId = 0;
	mtpRequestId _contactsStatusesRequestId = 0;
	base::flat_set<not_null<Data::Feed*>> _dialogFeedRequests;
	base::flat_map<
		not_null<History*>,
		std::vector<Fn<void()>>> _dialogRequests;

	base::flat_map<not_null<History*>, mtpRequestId> _unreadMentionsRequests;

	base::flat_map<
		not_null<ChatData*>,
		mtpRequestId> _chatAdminsEnabledRequests;
	base::flat_map<
		not_null<ChatData*>,
		base::flat_set<not_null<UserData*>>> _chatAdminsToSave;
	base::flat_map<
		not_null<ChatData*>,
		base::flat_set<mtpRequestId>> _chatAdminsSaveRequests;

	base::flat_map<std::tuple<
		not_null<PeerData*>,
		SharedMediaType,
		MsgId,
		SliceType>, mtpRequestId> _sharedMediaRequests;

	base::flat_map<not_null<UserData*>, mtpRequestId> _userPhotosRequests;

	base::flat_set<not_null<Data::Feed*>> _feedChannelsGetRequests;
	base::flat_map<
		not_null<Data::Feed*>,
		mtpRequestId> _feedChannelsSetRequests;
	base::flat_set<std::tuple<
		not_null<Data::Feed*>,
		Data::MessagePosition,
		SliceType>> _feedMessagesRequests;
	base::flat_set<std::tuple<
		not_null<Data::Feed*>,
		Data::MessagePosition,
		SliceType>> _feedMessagesRequestsPending;
	mtpRequestId _saveDefaultFeedIdRequest = 0;

	rpl::event_stream<SendOptions> _sendActions;

	struct ReadRequest {
		ReadRequest(mtpRequestId requestId, MsgId upTo)
		: requestId(requestId)
		, upTo(upTo) {
		}

		mtpRequestId requestId = 0;
		MsgId upTo = 0;
	};
	base::flat_map<not_null<PeerData*>, ReadRequest> _readRequests;
	base::flat_map<not_null<PeerData*>, MsgId> _readRequestsPending;

	std::unique_ptr<TaskQueue> _fileLoader;
	base::flat_map<uint64, std::shared_ptr<SendingAlbum>> _sendingAlbums;

	base::Observable<PeerData*> _fullPeerUpdated;

	rpl::event_stream<uint64> _stickerSetInstalled;

	base::flat_map<not_null<Data::Feed*>, TimeMs> _feedReadsDelayed;
	base::flat_map<not_null<Data::Feed*>, mtpRequestId> _feedReadRequests;
	base::Timer _feedReadTimer;

	mtpRequestId _proxyPromotionRequestId = 0;
	std::pair<QString, uint32> _proxyPromotionKey;
	TimeId _proxyPromotionNextRequestTime = TimeId(0);
	base::Timer _proxyPromotionTimer;

	base::flat_set<not_null<const PeerData*>> _updateNotifySettingsPeers;
	base::Timer _updateNotifySettingsTimer;

	std::map<
		Data::FileOrigin,
		std::vector<FileReferencesHandler>> _fileReferenceHandlers;

	mtpRequestId _deepLinkInfoRequestId = 0;

	TimeMs _termsUpdateSendAt = 0;
	mtpRequestId _termsUpdateRequestId = 0;

	mtpRequestId _checkInviteRequestId = 0;
	FnMut<void(const MTPChatInvite &result)> _checkInviteDone;
	FnMut<void(const RPCError &error)> _checkInviteFail;

	std::vector<FnMut<void(const MTPUser &)>> _supportContactCallbacks;

	base::flat_map<FullMsgId, not_null<PeerData*>> _peerPhotoUploads;

	mtpRequestId _passwordRequestId = 0;
	std::unique_ptr<Core::CloudPasswordState> _passwordState;
	rpl::event_stream<Core::CloudPasswordState> _passwordStateChanges;

	mtpRequestId _saveBioRequestId = 0;
	FnMut<void()> _saveBioDone;
	QString _saveBioText;

	base::flat_map<Privacy::Key, mtpRequestId> _privacyRequestIds;
	base::flat_map<Privacy::Key, Privacy> _privacyValues;
	std::map<Privacy::Key, rpl::event_stream<Privacy>> _privacyChanges;

	mtpRequestId _selfDestructRequestId = 0;
	std::optional<int> _selfDestructDays;
	rpl::event_stream<int> _selfDestructChanges;

	base::flat_map<FullMsgId, mtpRequestId> _pollVotesRequestIds;
	base::flat_map<FullMsgId, mtpRequestId> _pollCloseRequestIds;

};
