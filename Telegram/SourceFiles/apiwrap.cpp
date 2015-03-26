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
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "application.h"
#include "window.h"
#include "mainwidget.h"
#include "apiwrap.h"

#include "localstorage.h"

ApiWrap::ApiWrap(QObject *parent) : QObject(parent) {
	App::initBackground();

	connect(&_replyToTimer, SIGNAL(timeout()), this, SLOT(resolveReplyTo()));
}

void ApiWrap::init() {
	App::initMedia();
}

void ApiWrap::itemRemoved(HistoryItem *item) {
	if (HistoryReply *reply = item->toHistoryReply()) {
		ReplyToRequests::iterator i = _replyToRequests.find(reply->replyToId());
		if (i != _replyToRequests.cend()) {
			for (QList<HistoryReply*>::iterator j = i->replies.begin(); j != i->replies.end();) {
				if ((*j) == reply) {
					j = i->replies.erase(j);
				} else {
					++j;
				}
			}
			if (i->replies.isEmpty()) {
				_replyToRequests.erase(i);
			}
		}
	}
}

void ApiWrap::itemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	if (HistoryReply *reply = oldItem->toHistoryReply()) {
		ReplyToRequests::iterator i = _replyToRequests.find(reply->replyToId());
		if (i != _replyToRequests.cend()) {
			for (QList<HistoryReply*>::iterator j = i->replies.begin(); j != i->replies.end();) {
				if ((*j) == reply) {
					if (HistoryReply *newReply = newItem->toHistoryReply()) {
						*j = newReply;
						++j;
					} else {
						j = i->replies.erase(j);
					}
				} else {
					++j;
				}
			}
			if (i->replies.isEmpty()) {
				_replyToRequests.erase(i);
			}
		}
	}
}

void ApiWrap::requestReplyTo(HistoryReply *reply, MsgId to) {
	ReplyToRequest &req(_replyToRequests[to]);
	req.replies.append(reply);
	if (!req.req) _replyToTimer.start(1);
}

void ApiWrap::requestFullPeer(PeerData *peer) {
	if (_fullRequests.contains(peer)) return;
	mtpRequestId req;
	if (peer->chat) {
		req = MTP::send(MTPmessages_GetFullChat(MTP_int(App::chatFromPeer(peer->id))), rpcDone(&ApiWrap::gotChatFull, peer), rpcFail(&ApiWrap::gotPeerFailed, peer));
	} else {
		req = MTP::send(MTPusers_GetFullUser(peer->asUser()->inputUser), rpcDone(&ApiWrap::gotUserFull, peer), rpcFail(&ApiWrap::gotPeerFailed, peer));
	}
	_fullRequests.insert(peer, req);
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result) {
	const MTPDmessages_chatFull &d(result.c_messages_chatFull());
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	App::feedParticipants(d.vfull_chat.c_chatFull().vparticipants);
	PhotoData *photo = App::feedPhoto(d.vfull_chat.c_chatFull().vchat_photo);
	if (photo) {
		ChatData *chat = peer->asChat();
		if (chat) {
			chat->photoId = photo->id;
			photo->chat = chat;
		}
	}
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), d.vfull_chat.c_chatFull().vnotify_settings);

	_fullRequests.remove(peer);
	emit fullPeerLoaded(peer);
}

void ApiWrap::gotUserFull(PeerData *peer, const MTPUserFull &result) {
	const MTPDuserFull &d(result.c_userFull());
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	App::feedUserLink(MTP_int(App::userFromPeer(peer->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), d.vnotify_settings);

	_fullRequests.remove(peer);
	emit fullPeerLoaded(peer);
}

bool ApiWrap::gotPeerFailed(PeerData *peer, const RPCError &err) {
	_fullRequests.remove(peer);
	return true;
}

void ApiWrap::resolveReplyTo() {
	if (_replyToRequests.isEmpty()) return;

	QVector<MTPint> ids;
	ids.reserve(_replyToRequests.size());
	for (ReplyToRequests::const_iterator i = _replyToRequests.cbegin(), e = _replyToRequests.cend(); i != e; ++i) {
		if (!i.value().req) {
			ids.push_back(MTP_int(i.key()));
		}
	}
	if (!ids.isEmpty()) {
		mtpRequestId req = MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotReplyTo));
		for (ReplyToRequests::iterator i = _replyToRequests.begin(), e = _replyToRequests.end(); i != e; ++i) {
			i.value().req = req;
		}
	}
}

void ApiWrap::gotReplyTo(const MTPmessages_Messages &msgs, mtpRequestId req) {
	switch (msgs.type()) {
	case mtpc_messages_messages:
		App::feedUsers(msgs.c_messages_messages().vusers);
		App::feedChats(msgs.c_messages_messages().vchats);
		App::feedMsgs(msgs.c_messages_messages().vmessages, -1);
		break;

	case mtpc_messages_messagesSlice:
		App::feedUsers(msgs.c_messages_messagesSlice().vusers);
		App::feedChats(msgs.c_messages_messagesSlice().vchats);
		App::feedMsgs(msgs.c_messages_messagesSlice().vmessages, -1);
		break;
	}
	for (ReplyToRequests::iterator i = _replyToRequests.begin(); i != _replyToRequests.cend();) {
		if (i.value().req == req) {
			for (QList<HistoryReply*>::const_iterator j = i.value().replies.cbegin(), e = i.value().replies.cend(); j != e; ++j) {
				if (*j) {
					(*j)->updateReplyTo(true);
				} else {
					App::main()->updateReplyTo();
				}
			}
			i = _replyToRequests.erase(i);
		} else {
			++i;
		}
	}
}

ApiWrap::~ApiWrap() {
	App::deinitMedia(false);
}
