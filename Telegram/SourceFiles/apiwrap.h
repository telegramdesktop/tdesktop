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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

class ApiWrap : public QObject, public RPCSender {
	Q_OBJECT

public:

	ApiWrap(QObject *parent);
	void init();

	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);
		
	void requestReplyTo(HistoryReply *reply, MsgId to);

	void requestFullPeer(PeerData *peer);

	void requestWebPageDelayed(WebPageData *page);
	void clearWebPageRequest(WebPageData *page);
	void clearWebPageRequests();

	~ApiWrap();

signals:

	void fullPeerLoaded(PeerData *peer);

public slots:

	void resolveReplyTo();
	void resolveWebPages();

private:

	void gotReplyTo(const MTPmessages_Messages &result, mtpRequestId req);
	struct ReplyToRequest {
		ReplyToRequest() : req(0) {
		}
		mtpRequestId req;
		QList<HistoryReply*> replies;
	};
	typedef QMap<MsgId, ReplyToRequest> ReplyToRequests;
	ReplyToRequests _replyToRequests;
	SingleTimer _replyToTimer;

	void gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result);
	void gotUserFull(PeerData *peer, const MTPUserFull &result);
	bool gotPeerFailed(PeerData *peer, const RPCError &err);
	typedef QMap<PeerData*, mtpRequestId> FullRequests;
	FullRequests _fullRequests;

	void gotWebPages(const MTPmessages_Messages &result, mtpRequestId req);
	typedef QMap<WebPageData*, mtpRequestId> WebPagesPending;
	WebPagesPending _webPagesPending;
	SingleTimer _webPagesTimer;

};
