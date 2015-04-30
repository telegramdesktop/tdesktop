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
	connect(&_webPagesTimer, SIGNAL(timeout()), this, SLOT(resolveWebPages()));
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

void ApiWrap::requestWebPageDelayed(WebPageData *page) {
	if (page->pendingTill <= 0) return;
	_webPagesPending.insert(page, 0);
	int32 left = (page->pendingTill - unixtime()) * 1000;
	if (!_webPagesTimer.isActive() || left <= _webPagesTimer.remainingTime()) {
		_webPagesTimer.start((left < 0 ? 0 : left) + 1);
	}
}

void ApiWrap::clearWebPageRequest(WebPageData *page) {
	_webPagesPending.remove(page);
	if (_webPagesPending.isEmpty() && _webPagesTimer.isActive()) _webPagesTimer.stop();
}

void ApiWrap::clearWebPageRequests() {
	_webPagesPending.clear();
	_webPagesTimer.stop();
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result) {
	const MTPDmessages_chatFull &d(result.c_messages_chatFull());
	const MTPDchatFull &f(d.vfull_chat.c_chatFull());
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	App::feedParticipants(f.vparticipants);
	PhotoData *photo = App::feedPhoto(f.vchat_photo);
	ChatData *chat = peer->asChat();
	if (chat) {
		if (photo) {
			chat->photoId = photo->id;
			photo->chat = chat;
		}
		chat->invitationUrl = (f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString();
	}

	App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);

	_fullRequests.remove(peer);
	emit fullPeerLoaded(peer);
}

void ApiWrap::gotUserFull(PeerData *peer, const MTPUserFull &result) {
	const MTPDuserFull &d(result.c_userFull());
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	App::feedPhoto(d.vprofile_photo);
	App::feedUserLink(MTP_int(App::userFromPeer(peer->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), d.vnotify_settings);

	_fullRequests.remove(peer);
	emit fullPeerLoaded(peer);
}

bool ApiWrap::gotPeerFailed(PeerData *peer, const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

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

void ApiWrap::resolveWebPages() {
	QVector<MTPint> ids;
	const WebPageItems &items(App::webPageItems());
	ids.reserve(_webPagesPending.size());
	int32 t = unixtime(), m = INT_MAX;
	for (WebPagesPending::const_iterator i = _webPagesPending.cbegin(), e = _webPagesPending.cend(); i != e; ++i) {
		if (i.value()) continue;
		if (i.key()->pendingTill <= t) {
			WebPageItems::const_iterator j = items.constFind(i.key());
			if (j != items.cend() && !j.value().isEmpty()) {
				ids.push_back(MTP_int(j.value().begin().key()->id));
			}
		} else {
			m = qMin(m, i.key()->pendingTill - t);
		}
	}
	if (!ids.isEmpty()) {
		mtpRequestId req = MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotWebPages));
		for (WebPagesPending::iterator i = _webPagesPending.begin(); i != _webPagesPending.cend(); ++i) {
			if (i.value()) continue;
			if (i.key()->pendingTill <= t) {
				i.value() = req;
			}
		}
	}
	if (m < INT_MAX) _webPagesTimer.start(m * 1000);
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

void ApiWrap::gotWebPages(const MTPmessages_Messages &msgs, mtpRequestId req) {
	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages:
		App::feedUsers(msgs.c_messages_messages().vusers);
		App::feedChats(msgs.c_messages_messages().vchats);
		v = &msgs.c_messages_messages().vmessages.c_vector().v;
		break;

	case mtpc_messages_messagesSlice:
		App::feedUsers(msgs.c_messages_messagesSlice().vusers);
		App::feedChats(msgs.c_messages_messagesSlice().vchats);
		v = &msgs.c_messages_messagesSlice().vmessages.c_vector().v;
		break;
	}

	QMap<int32, int32> msgsIds; // copied from feedMsgs
	for (int32 i = 0, l = v->size(); i < l; ++i) {
		const MTPMessage &msg(v->at(i));
		switch (msg.type()) {
		case mtpc_message: msgsIds.insert(msg.c_message().vid.v, i); break;
		case mtpc_messageEmpty: msgsIds.insert(msg.c_messageEmpty().vid.v, i); break;
		case mtpc_messageService: msgsIds.insert(msg.c_messageService().vid.v, i); break;
		}
	}

	MainWidget *m = App::main();
	for (QMap<int32, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addToBack(v->at(*i), -1);
		if (item) {
			item->initDimensions();
			if (m) m->itemResized(item);
		}
	}

	const WebPageItems &items(App::webPageItems());
	for (WebPagesPending::iterator i = _webPagesPending.begin(); i != _webPagesPending.cend();) {
		if (i.value() == req) {
			if (i.key()->pendingTill > 0) {
				i.key()->pendingTill = -1;
				WebPageItems::const_iterator j = items.constFind(i.key());
				if (j != items.cend()) {
					for (HistoryItemsMap::const_iterator k = j.value().cbegin(), e = j.value().cend(); k != e; ++k) {
						k.key()->initDimensions();
						if (m) m->itemResized(k.key());
					}
				}
			}
			i = _webPagesPending.erase(i);
		} else {
			++i;
		}
	}
}

ApiWrap::~ApiWrap() {
	App::deinitMedia(false);
}
