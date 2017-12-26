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

#include <rpl/event_stream.h>
#include "base/timer.h"
#include "core/single_timer.h"
#include "mtproto/sender.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "chat_helpers/stickers.h"

class TaskQueue;
class AuthSession;
enum class SparseIdsLoadDirection;
struct MessageGroupId;
struct SendingAlbum;
enum class SendMediaType;

namespace Storage {
enum class SharedMediaType : char;
struct PreparedList;
} // namespace Storage

namespace Api {

inline const MTPVector<MTPChat> *getChatsFromMessagesChats(const MTPmessages_Chats &chats) {
	switch (chats.type()) {
	case mtpc_messages_chats: return &chats.c_messages_chats().vchats;
	case mtpc_messages_chatsSlice: return &chats.c_messages_chatsSlice().vchats;
	}
	return nullptr;
}

} // namespace Api

class ApiWrap : private MTP::Sender, private base::Subscriber {
public:
	ApiWrap(not_null<AuthSession*> session);

	void start();
	void applyUpdates(const MTPUpdates &updates, uint64 sentMessageRandomId = 0);

	using RequestMessageDataCallback = base::lambda<void(ChannelData*, MsgId)>;
	void requestMessageData(ChannelData *channel, MsgId msgId, RequestMessageDataCallback callback);

	void requestFullPeer(PeerData *peer);
	void requestPeer(PeerData *peer);
	void requestPeers(const QList<PeerData*> &peers);
	void requestLastParticipants(not_null<ChannelData*> channel);
	void requestBots(not_null<ChannelData*> channel);
	void requestAdmins(not_null<ChannelData*> channel);
	void requestParticipantsCountDelayed(not_null<ChannelData*> channel);

	void requestChannelMembersForAdd(
		not_null<ChannelData*> channel,
		base::lambda<void(const MTPchannels_ChannelParticipants&)> callback);
	void processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result);
	void processFullPeer(UserData *user, const MTPUserFull &result);

	void requestSelfParticipant(ChannelData *channel);
	void kickParticipant(not_null<ChatData*> chat, not_null<UserData*> user);
	void kickParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChannelBannedRights &currentRights);
	void unblockParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);

	void requestWebPageDelayed(WebPageData *page);
	void clearWebPageRequest(WebPageData *page);
	void clearWebPageRequests();

	void scheduleStickerSetRequest(uint64 setId, uint64 access);
	void requestStickerSets();
	void saveStickerSets(const Stickers::Order &localOrder, const Stickers::Order &localRemoved);
	void updateStickers();
	void setGroupStickerSet(not_null<ChannelData*> megagroup, const MTPInputStickerSet &set);

	void joinChannel(ChannelData *channel);
	void leaveChannel(ChannelData *channel);

	void blockUser(UserData *user);
	void unblockUser(UserData *user);

	void exportInviteLink(PeerData *peer);
	void requestNotifySetting(PeerData *peer);

	void saveDraftToCloudDelayed(History *history);

	void savePrivacy(const MTPInputPrivacyKey &key, QVector<MTPInputPrivacyRule> &&rules);
	void handlePrivacyChange(mtpTypeId keyTypeId, const MTPVector<MTPPrivacyRule> &rules);
	int onlineTillFromStatus(const MTPUserStatus &status, int currentOnlineTill);

	void clearHistory(not_null<PeerData*> peer);

	base::Observable<PeerData*> &fullPeerUpdated() {
		return _fullPeerUpdated;
	}

	bool isQuitPrevent();

	void applyUpdatesNoPtsCheck(const MTPUpdates &updates);
	void applyUpdateNoPtsCheck(const MTPUpdate &update);

	void jumpToDate(not_null<PeerData*> peer, const QDate &date);

	void preloadEnoughUnreadMentions(not_null<History*> history);
	void checkForUnreadMentions(const base::flat_set<MsgId> &possiblyReadMentions, ChannelData *channel = nullptr);

	void editChatAdmins(
		not_null<ChatData*> chat,
		bool adminsEnabled,
		base::flat_set<not_null<UserData*>> &&admins);

	using SliceType = SparseIdsLoadDirection;
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
		base::lambda<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		base::lambda<void()> callbackNotModified = nullptr);
	void parseRecentChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		base::lambda<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		base::lambda<void()> callbackNotModified = nullptr);

	struct SendOptions {
		SendOptions(not_null<History*> history) : history(history) {
		}

		not_null<History*> history;
		MsgId replyTo = 0;
		WebPageId webPageId = 0;
		bool clearDraft = false;
		bool generateLocal = true;
	};
	rpl::producer<SendOptions> sendActions() const {
		return _sendActions.events();
	}
	void sendAction(const SendOptions &options);
	void forwardMessages(
		HistoryItemsList &&items,
		const SendOptions &options,
		base::lambda_once<void()> &&successCallback = nullptr);
	void shareContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		const SendOptions &options);
	void shareContact(not_null<UserData*> user, const SendOptions &options);
	void readServerHistory(not_null<History*> history);
	void readServerHistoryForce(not_null<History*> history);

	void sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		int duration,
		const SendOptions &options);
	void sendFiles(
		Storage::PreparedList &&list,
		SendMediaType type,
		QString caption,
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
		const base::optional<MTPInputFile> &thumb,
		bool silent);
	void cancelLocalItem(not_null<HistoryItem*> item);

	~ApiWrap();

private:
	struct MessageDataRequest {
		using Callbacks = QList<RequestMessageDataCallback>;
		mtpRequestId requestId = 0;
		Callbacks callbacks;
	};
	using MessageDataRequests = QMap<MsgId, MessageDataRequest>;
	using SharedMediaType = Storage::SharedMediaType;

	void requestAppChangelogs();
	void addLocalChangelogs(int oldAppVersion);
	void updatesReceived(const MTPUpdates &updates);
	void checkQuitPreventFinished();

	void saveDraftsToCloud();

	void resolveMessageDatas();
	void gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId requestId);
	void finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId);

	QVector<MTPint> collectMessageIds(const MessageDataRequests &requests);
	MessageDataRequests *messageDataRequests(ChannelData *channel, bool onlyExisting = false);

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
	void gotWebPages(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId req);
	void gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result);

	PeerData *notifySettingReceived(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);

	void stickerSetDisenabled(mtpRequestId requestId);
	void stickersSaveOrder();

	void requestStickers(TimeId now);
	void requestRecentStickers(TimeId now);
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

	template <typename Callback>
	void requestMessageAfterDate(
		not_null<PeerData*> peer,
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

	not_null<AuthSession*> _session;
	mtpRequestId _changelogSubscription = 0;

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
	base::lambda<void(const MTPchannels_ChannelParticipants&)> _channelMembersForAddCallback;

	using KickRequest = std::pair<
		not_null<ChannelData*>,
		not_null<UserData*>>;
	base::flat_map<KickRequest, mtpRequestId> _kickRequests;

	QMap<ChannelData*, mtpRequestId> _selfParticipantRequests;

	QMap<WebPageData*, mtpRequestId> _webPagesPending;
	base::Timer _webPagesTimer;

	QMap<uint64, QPair<uint64, mtpRequestId> > _stickerSetRequests;

	QMap<ChannelData*, mtpRequestId> _channelAmInRequests;
	QMap<UserData*, mtpRequestId> _blockRequests;
	QMap<PeerData*, mtpRequestId> _exportInviteRequests;

	QMap<PeerData*, mtpRequestId> _notifySettingRequests;

	QMap<History*, mtpRequestId> _draftsSaveRequestIds;
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

	QMap<mtpTypeId, mtpRequestId> _privacySaveRequests;

	mtpRequestId _contactsStatusesRequestId = 0;

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

};
