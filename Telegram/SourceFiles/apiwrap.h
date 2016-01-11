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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

class ApiWrap : public QObject, public RPCSender {
	Q_OBJECT

public:

	ApiWrap(QObject *parent);
	void init();

	void itemRemoved(HistoryItem *item);
		
	void requestReplyTo(HistoryReply *reply, ChannelData *channel, MsgId id);

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

	~ApiWrap();

signals:

	void fullPeerUpdated(PeerData *peer);

public slots:

	void resolveReplyTo();
	void resolveWebPages();

	void delayedRequestParticipantsCount();

private:

	void gotReplyTo(ChannelData *channel, const MTPmessages_Messages &result, mtpRequestId req);
	struct ReplyToRequest {
		ReplyToRequest() : req(0) {
		}
		mtpRequestId req;
		QList<HistoryReply*> replies;
	};
	typedef QMap<MsgId, ReplyToRequest> ReplyToRequests;
	ReplyToRequests _replyToRequests;
	typedef QMap<ChannelData*, ReplyToRequests> ChannelReplyToRequests;
	ChannelReplyToRequests _channelReplyToRequests;
	SingleTimer _replyToTimer;
	typedef QVector<MTPint> MessageIds;
	MessageIds collectMessageIds(const ReplyToRequests &requests);
	ReplyToRequests *replyToRequests(ChannelData *channel, bool onlyExisting = false);

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

};
