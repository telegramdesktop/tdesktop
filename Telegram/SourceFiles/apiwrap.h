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

#include "core/single_timer.h"

namespace Api {

inline const MTPVector<MTPChat> *getChatsFromMessagesChats(const MTPmessages_Chats &chats) {
	switch (chats.type()) {
	case mtpc_messages_chats: return &chats.c_messages_chats().vchats;
	case mtpc_messages_chatsSlice: return &chats.c_messages_chatsSlice().vchats;
	}
	return nullptr;
}

} // namespace Api

class ApiWrap : public QObject, public RPCSender {
	Q_OBJECT

public:
	ApiWrap(QObject *parent);
	void init();

	using RequestMessageDataCallback = base::lambda_copy<void(ChannelData*, MsgId)>;
	void requestMessageData(ChannelData *channel, MsgId msgId, const RequestMessageDataCallback &callback);

	void requestFullPeer(PeerData *peer);
	void requestPeer(PeerData *peer);
	void requestPeers(const QList<PeerData*> &peers);
	void requestLastParticipants(ChannelData *peer, bool fromStart = true);
	void requestBots(ChannelData *peer);

	void processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result);
	void processFullPeer(PeerData *peer, const MTPUserFull &result);

	void requestSelfParticipant(ChannelData *channel);
	void kickParticipant(PeerData *peer, UserData *user);

	void requestWebPageDelayed(WebPageData *page);
	void clearWebPageRequest(WebPageData *page);
	void clearWebPageRequests();

	void scheduleStickerSetRequest(uint64 setId, uint64 access);
	void requestStickerSets();
	void saveStickerSets(const Stickers::Order &localOrder, const Stickers::Order &localRemoved);

	void joinChannel(ChannelData *channel);
	void leaveChannel(ChannelData *channel);

	void blockUser(UserData *user);
	void unblockUser(UserData *user);

	void exportInviteLink(PeerData *peer);
	void requestNotifySetting(PeerData *peer);

	void saveDraftToCloudDelayed(History *history);
	bool hasUnsavedDrafts() const;

	~ApiWrap();

signals:
	void fullPeerUpdated(PeerData *peer);

public slots:
	void resolveMessageDatas();
	void resolveWebPages();

	void delayedRequestParticipantsCount();
	void saveDraftsToCloud();

private:
	void updatesReceived(const MTPUpdates &updates);

	void gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId req);
	struct MessageDataRequest {
		using Callbacks = QList<RequestMessageDataCallback>;
		mtpRequestId req = 0;
		Callbacks callbacks;
	};
	typedef QMap<MsgId, MessageDataRequest> MessageDataRequests;
	MessageDataRequests _messageDataRequests;
	typedef QMap<ChannelData*, MessageDataRequests> ChannelMessageDataRequests;
	ChannelMessageDataRequests _channelMessageDataRequests;
	SingleDelayedCall *_messageDataResolveDelayed;
	typedef QVector<MTPint> MessageIds;
	MessageIds collectMessageIds(const MessageDataRequests &requests);
	MessageDataRequests *messageDataRequests(ChannelData *channel, bool onlyExisting = false);

	void gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req);
	void gotUserFull(PeerData *peer, const MTPUserFull &result, mtpRequestId req);
	bool gotPeerFullFailed(PeerData *peer, const RPCError &err);
	typedef QMap<PeerData*, mtpRequestId> PeerRequests;
	PeerRequests _fullPeerRequests;

	void gotChat(PeerData *peer, const MTPmessages_Chats &result);
	void gotUser(PeerData *peer, const MTPVector<MTPUser> &result);
	void gotChats(const MTPmessages_Chats &result);
	void gotUsers(const MTPVector<MTPUser> &result);
	bool gotPeerFailed(PeerData *peer, const RPCError &err);
	PeerRequests _peerRequests;

	void lastParticipantsDone(ChannelData *peer, const MTPchannels_ChannelParticipants &result, mtpRequestId req);
	bool lastParticipantsFail(ChannelData *peer, const RPCError &error, mtpRequestId req);
	PeerRequests _participantsRequests, _botsRequests;

	typedef QPair<PeerData*, UserData*> KickRequest;
	typedef QMap<KickRequest, mtpRequestId> KickRequests;
	void kickParticipantDone(KickRequest kick, const MTPUpdates &updates, mtpRequestId req);
	bool kickParticipantFail(KickRequest kick, const RPCError &error, mtpRequestId req);
	KickRequests _kickRequests;

	void gotSelfParticipant(ChannelData *channel, const MTPchannels_ChannelParticipant &result);
	bool gotSelfParticipantFail(ChannelData *channel, const RPCError &error);
	typedef QMap<ChannelData*, mtpRequestId> SelfParticipantRequests;
	SelfParticipantRequests _selfParticipantRequests;

	void gotWebPages(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId req);
	typedef QMap<WebPageData*, mtpRequestId> WebPagesPending;
	WebPagesPending _webPagesPending;
	SingleTimer _webPagesTimer;

	QMap<uint64, QPair<uint64, mtpRequestId> > _stickerSetRequests;
	void gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result);
	bool gotStickerSetFail(uint64 setId, const RPCError &error);

	QMap<ChannelData*, mtpRequestId> _channelAmInRequests;
	void channelAmInUpdated(ChannelData *channel);
	void channelAmInDone(ChannelData *channel, const MTPUpdates &updates);
	bool channelAmInFail(ChannelData *channel, const RPCError &error);

	QMap<UserData*, mtpRequestId> _blockRequests;
	void blockDone(UserData *user, const MTPBool &result);
	void unblockDone(UserData *user, const MTPBool &result);
	bool blockFail(UserData *user, const RPCError &error);

	QMap<PeerData*, mtpRequestId> _exportInviteRequests;
	void exportInviteDone(PeerData *peer, const MTPExportedChatInvite &result);
	bool exportInviteFail(PeerData *peer, const RPCError &error);

	QMap<PeerData*, mtpRequestId> _notifySettingRequests;
	void notifySettingDone(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	PeerData *notifySettingReceived(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	bool notifySettingFail(PeerData *peer, const RPCError &error);

	QMap<History*, mtpRequestId> _draftsSaveRequestIds;
	SingleTimer _draftsSaveTimer;
	void saveCloudDraftDone(History *history, const MTPBool &result, mtpRequestId requestId);
	bool saveCloudDraftFail(History *history, const RPCError &error, mtpRequestId requestId);

	OrderedSet<mtpRequestId> _stickerSetDisenableRequests;
	void stickerSetDisenableDone(const MTPmessages_StickerSetInstallResult &result, mtpRequestId req);
	bool stickerSetDisenableFail(const RPCError &error, mtpRequestId req);
	Stickers::Order _stickersOrder;
	mtpRequestId _stickersReorderRequestId = 0;
	void stickersSaveOrder();
	void stickersReorderDone(const MTPBool &result);
	bool stickersReorderFail(const RPCError &result);
	mtpRequestId _stickersClearRecentRequestId = 0;
	void stickersClearRecentDone(const MTPBool &result);
	bool stickersClearRecentFail(const RPCError &result);

};
