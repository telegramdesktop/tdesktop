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

#include "base/timer.h"
#include "core/single_timer.h"
#include "mtproto/sender.h"
#include "base/flat_map.h"
#include "base/flat_set.h"

class AuthSession;

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
	void requestLastParticipants(ChannelData *channel, bool fromStart = true);
	void requestBots(ChannelData *channel);
	void requestParticipantsCountDelayed(ChannelData *channel);

	void processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result);
	void processFullPeer(UserData *user, const MTPUserFull &result);

	void requestSelfParticipant(ChannelData *channel);
	void kickParticipant(PeerData *peer, UserData *user, const MTPChannelBannedRights &currentRights);
	void unblockParticipant(PeerData *peer, UserData *user);

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

	~ApiWrap();

private:
	struct MessageDataRequest {
		using Callbacks = QList<RequestMessageDataCallback>;
		mtpRequestId requestId = 0;
		Callbacks callbacks;
	};
	using MessageDataRequests = QMap<MsgId, MessageDataRequest>;

	void requestAppChangelogs();
	void addLocalChangelogs(int oldAppVersion);
	void updatesReceived(const MTPUpdates &updates);
	void checkQuitPreventFinished();

	void saveDraftsToCloud();

	void resolveMessageDatas();
	void gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId requestId);

	QVector<MTPint> collectMessageIds(const MessageDataRequests &requests);
	MessageDataRequests *messageDataRequests(ChannelData *channel, bool onlyExisting = false);

	void gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req);
	void gotUserFull(UserData *user, const MTPUserFull &result, mtpRequestId req);
	void lastParticipantsDone(ChannelData *peer, const MTPchannels_ChannelParticipants &result, mtpRequestId req);
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

	void cancelEditChatAdmins(not_null<ChatData*> chat);
	void saveChatAdmins(not_null<ChatData*> chat);
	void sendSaveChatAdminsRequests(not_null<ChatData*> chat);

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
	base::DelayedCallTimer _participantsCountRequestTimer;

	typedef QPair<PeerData*, UserData*> KickRequest;
	typedef QMap<KickRequest, mtpRequestId> KickRequests;
	KickRequests _kickRequests;

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

	OrderedSet<mtpRequestId> _stickerSetDisenableRequests;
	Stickers::Order _stickersOrder;
	mtpRequestId _stickersReorderRequestId = 0;
	mtpRequestId _stickersClearRecentRequestId = 0;

	mtpRequestId _stickersUpdateRequest = 0;
	mtpRequestId _recentStickersUpdateRequest = 0;
	mtpRequestId _favedStickersUpdateRequest = 0;
	mtpRequestId _featuredStickersUpdateRequest = 0;
	mtpRequestId _savedGifsUpdateRequest = 0;

	QMap<mtpTypeId, mtpRequestId> _privacySaveRequests;

	mtpRequestId _contactsStatusesRequestId = 0;

	base::flat_map<not_null<History*>, mtpRequestId> _unreadMentionsRequests;

	base::flat_map<not_null<ChatData*>, mtpRequestId> _chatAdminsEnabledRequests;
	base::flat_map<not_null<ChatData*>, base::flat_set<not_null<UserData*>>> _chatAdminsToSave;
	base::flat_map<not_null<ChatData*>, base::flat_set<mtpRequestId>> _chatAdminsSaveRequests;

	base::Observable<PeerData*> _fullPeerUpdated;

};
