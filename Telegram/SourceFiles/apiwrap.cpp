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
}

void ApiWrap::itemRemoved(HistoryItem *item) {
	if (HistoryReply *reply = item->toHistoryReply()) {
		ChannelData *channel = reply->history()->peer->asChannel();
		ReplyToRequests *requests(replyToRequests(channel, true));
		if (requests) {
			ReplyToRequests::iterator i = requests->find(reply->replyToId());
			if (i != requests->cend()) {
				for (QList<HistoryReply*>::iterator j = i->replies.begin(); j != i->replies.end();) {
					if ((*j) == reply) {
						j = i->replies.erase(j);
					} else {
						++j;
					}
				}
				if (i->replies.isEmpty()) {
					requests->erase(i);
				}
			}
			if (channel && requests->isEmpty()) {
				_channelReplyToRequests.remove(channel);
			}
		}
	}
}

void ApiWrap::itemReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	if (HistoryReply *reply = oldItem->toHistoryReply()) {
		ChannelData *channel = reply->history()->peer->asChannel();
		ReplyToRequests *requests(replyToRequests(channel, true));
		if (requests) {
			ReplyToRequests::iterator i = requests->find(reply->replyToId());
			if (i != requests->cend()) {
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
					requests->erase(i);
				}
			}
			if (channel && requests->isEmpty()) {
				_channelReplyToRequests.remove(channel);
			}
		}
	}
}

void ApiWrap::requestReplyTo(HistoryReply *reply, ChannelData *channel, MsgId id) {
	ReplyToRequest &req(channel ? _channelReplyToRequests[channel][id] : _replyToRequests[id]);
	req.replies.append(reply);
	if (!req.req) _replyToTimer.start(1);
}

ApiWrap::MessageIds ApiWrap::collectMessageIds(const ReplyToRequests &requests) {
	MessageIds result;
	result.reserve(requests.size());
	for (ReplyToRequests::const_iterator i = requests.cbegin(), e = requests.cend(); i != e; ++i) {
		if (i.value().req > 0) continue;
		result.push_back(MTP_int(i.key()));
	}
	return result;
}

ApiWrap::ReplyToRequests *ApiWrap::replyToRequests(ChannelData *channel, bool onlyExisting) {
	if (channel) {
		ChannelReplyToRequests::iterator i = _channelReplyToRequests.find(channel);
		if (i == _channelReplyToRequests.cend()) {
			if (onlyExisting) return 0;
			i = _channelReplyToRequests.insert(channel, ReplyToRequests());
		}
		return &i.value();
	}
	return &_replyToRequests;
}

void ApiWrap::resolveReplyTo() {
	if (_replyToRequests.isEmpty() && _channelReplyToRequests.isEmpty()) return;

	MessageIds ids = collectMessageIds(_replyToRequests);
	if (!ids.isEmpty()) {
		mtpRequestId req = MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotReplyTo, (ChannelData*)0), RPCFailHandlerPtr(), 0, 5);
		for (ReplyToRequests::iterator i = _replyToRequests.begin(); i != _replyToRequests.cend(); ++i) {
			if (i.value().req > 0) continue;
			i.value().req = req;
		}
	}
	for (ChannelReplyToRequests::iterator j = _channelReplyToRequests.begin(); j != _channelReplyToRequests.cend();) {
		if (j->isEmpty()) {
			j = _channelReplyToRequests.erase(j);
			continue;
		}
		MessageIds ids = collectMessageIds(j.value());
		if (!ids.isEmpty()) {
			mtpRequestId req = MTP::send(MTPchannels_GetMessages(j.key()->inputChannel, MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotReplyTo, j.key()), RPCFailHandlerPtr(), 0, 5);
			for (ReplyToRequests::iterator i = j->begin(); i != j->cend(); ++i) {
				if (i.value().req > 0) continue;
				i.value().req = req;
			}
		}
		++j;
	}
}

void ApiWrap::gotReplyTo(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;

	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(msgs.c_messages_channelMessages());
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("App Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotReplyTo)"));
		}
		if (d.has_collapsed()) { // should not be returned
			LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (ApiWrap::gotReplyTo)"));
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;
	}
	ReplyToRequests *requests(replyToRequests(channel, true));
	if (requests) {
		for (ReplyToRequests::iterator i = requests->begin(); i != requests->cend();) {
			if (i.value().req == req) {
				for (QList<HistoryReply*>::const_iterator j = i.value().replies.cbegin(), e = i.value().replies.cend(); j != e; ++j) {
					if (*j) {
						(*j)->updateReplyTo(true);
					} else {
						App::main()->updateReplyTo();
					}
				}
				i = requests->erase(i);
			} else {
				++i;
			}
		}
		if (channel && requests->isEmpty()) {
			_channelReplyToRequests.remove(channel);
		}
	}
}

void ApiWrap::requestFullPeer(PeerData *peer) {
	if (!peer || _fullPeerRequests.contains(peer)) return;

	mtpRequestId req = 0;
	if (peer->isUser()) {
		req = MTP::send(MTPusers_GetFullUser(peer->asUser()->inputUser), rpcDone(&ApiWrap::gotUserFull, peer), rpcFail(&ApiWrap::gotPeerFullFailed, peer));
	} else if (peer->isChat()) {
		req = MTP::send(MTPmessages_GetFullChat(peer->asChat()->inputChat), rpcDone(&ApiWrap::gotChatFull, peer), rpcFail(&ApiWrap::gotPeerFullFailed, peer));
	} else if (peer->isChannel()) {
		req = MTP::send(MTPchannels_GetFullChannel(peer->asChannel()->inputChannel), rpcDone(&ApiWrap::gotChatFull, peer), rpcFail(&ApiWrap::gotPeerFullFailed, peer));
	}
	if (req) _fullPeerRequests.insert(peer, req);
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result) {
	const MTPDmessages_chatFull &d(result.c_messages_chatFull());
	const QVector<MTPChat> &vc(d.vchats.c_vector().v);
	bool badVersion = false;
	if (peer->isChat()) {
		badVersion = (!vc.isEmpty() && vc.at(0).type() == mtpc_chat && vc.at(0).c_chat().vversion.v < peer->asChat()->version);
	} else if (peer->isChannel()) {
		badVersion = (!vc.isEmpty() && vc.at(0).type() == mtpc_channel && vc.at(0).c_channel().vversion.v < peer->asChannel()->version);
	}

	App::feedUsers(d.vusers, false);
	App::feedChats(d.vchats, false);

	if (peer->isChat()) {
		if (d.vfull_chat.type() != mtpc_chatFull) {
			LOG(("MTP Error: bad type in gotChatFull for chat: %1").arg(d.vfull_chat.type()));
			return;
		}
		const MTPDchatFull &f(d.vfull_chat.c_chatFull());
		App::feedParticipants(f.vparticipants, false, false);
		const QVector<MTPBotInfo> &v(f.vbot_info.c_vector().v);
		for (QVector<MTPBotInfo>::const_iterator i = v.cbegin(), e = v.cend(); i < e; ++i) {
			switch (i->type()) {
			case mtpc_botInfo: {
				const MTPDbotInfo &b(i->c_botInfo());
				UserData *user = App::userLoaded(b.vuser_id.v);
				if (user) {
					user->setBotInfo(*i);
					App::clearPeerUpdated(user);
					emit fullPeerUpdated(user);
				}
			} break;
			}
		}
		PhotoData *photo = App::feedPhoto(f.vchat_photo);
		ChatData *chat = peer->asChat();
		if (photo) {
			chat->photoId = photo->id;
			photo->peer = chat;
		} else {
			chat->photoId = 0;
		}
		chat->invitationUrl = (f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString();

		App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	} else if (peer->isChannel()) {
		if (d.vfull_chat.type() != mtpc_channelFull) {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1").arg(d.vfull_chat.type()));
			return;
		}
		const MTPDchannelFull &f(d.vfull_chat.c_channelFull());
		PhotoData *photo = App::feedPhoto(f.vchat_photo);
		ChannelData *channel = peer->asChannel();
		channel->flagsFull = f.vflags.v;
		if (photo) {
			channel->photoId = photo->id;
			photo->peer = channel;
		} else {
			channel->photoId = 0;
		}
		channel->about = qs(f.vabout);
		channel->count = f.has_participants_count() ? f.vparticipants_count.v : 0;
		channel->adminsCount = f.has_admins_count() ? f.vadmins_count.v : 0;
		channel->invitationUrl = (f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString();
		if (History *h = App::historyLoaded(channel->id)) {
			if (h->inboxReadBefore < f.vread_inbox_max_id.v + 1) {
				h->setUnreadCount(f.vunread_important_count.v);
				h->inboxReadBefore = f.vread_inbox_max_id.v + 1;
				h->asChannelHistory()->unreadCountAll = f.vunread_count.v;
			}
		}
		channel->fullUpdated();

		App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	}

	_fullPeerRequests.remove(peer);
	if (badVersion) {
		if (peer->isChat()) {
			peer->asChat()->version = vc.at(0).c_chat().vversion.v;
		} else if (peer->isChannel()) {
			peer->asChannel()->version = vc.at(0).c_channel().vversion.v;
		}
		requestPeer(peer);
	}
	App::clearPeerUpdated(peer);
	emit fullPeerUpdated(peer);
	App::emitPeerUpdated();
}

void ApiWrap::gotUserFull(PeerData *peer, const MTPUserFull &result) {
	const MTPDuserFull &d(result.c_userFull());
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser), false);
	App::feedPhoto(d.vprofile_photo);
	App::feedUserLink(MTP_int(peerToUser(peer->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link, false);
	App::main()->gotNotifySetting(MTP_inputNotifyPeer(peer->input), d.vnotify_settings);

	peer->asUser()->setBotInfo(d.vbot_info);
	peer->asUser()->blocked = d.vblocked.v ? UserIsBlocked : UserIsNotBlocked;

	_fullPeerRequests.remove(peer);
	App::clearPeerUpdated(peer);
	emit fullPeerUpdated(peer);
	App::emitPeerUpdated();
}

bool ApiWrap::gotPeerFullFailed(PeerData *peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_fullPeerRequests.remove(peer);
	return true;
}

void ApiWrap::requestPeer(PeerData *peer) {
	if (!peer || _fullPeerRequests.contains(peer) || _peerRequests.contains(peer)) return;

	mtpRequestId req = 0;
	if (peer->isUser()) {
		req = MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, peer->asUser()->inputUser)), rpcDone(&ApiWrap::gotUser, peer), rpcFail(&ApiWrap::gotPeerFailed, peer));
	} else if (peer->isChat()) {
		req = MTP::send(MTPmessages_GetChats(MTP_vector<MTPint>(1, peer->asChat()->inputChat)), rpcDone(&ApiWrap::gotChat, peer), rpcFail(&ApiWrap::gotPeerFailed, peer));
	} else if (peer->isChannel()) {
		req = MTP::send(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(1, peer->asChannel()->inputChannel)), rpcDone(&ApiWrap::gotChat, peer), rpcFail(&ApiWrap::gotPeerFailed, peer));
	}
	if (req) _peerRequests.insert(peer, req);
}

void ApiWrap::requestPeers(const QList<PeerData*> &peers) {
	QVector<MTPint> chats;
	QVector<MTPInputChannel> channels;
	QVector<MTPInputUser> users;
	chats.reserve(peers.size());
	channels.reserve(peers.size());
	users.reserve(peers.size());
	for (QList<PeerData*>::const_iterator i = peers.cbegin(), e = peers.cend(); i != e; ++i) {
		if (!*i || _fullPeerRequests.contains(*i) || _peerRequests.contains(*i)) continue;
		if ((*i)->isUser()) {
			users.push_back((*i)->asUser()->inputUser);
		} else if ((*i)->isChat()) {
			chats.push_back((*i)->asChat()->inputChat);
		} else if ((*i)->isChannel()) {
			channels.push_back((*i)->asChannel()->inputChannel);
		}
	}
	if (!chats.isEmpty()) MTP::send(MTPmessages_GetChats(MTP_vector<MTPint>(chats)), rpcDone(&ApiWrap::gotChats));
	if (!channels.isEmpty()) MTP::send(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(channels)), rpcDone(&ApiWrap::gotChats));
	if (!users.isEmpty()) MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(users)), rpcDone(&ApiWrap::gotUsers));
}

void ApiWrap::gotChat(PeerData *peer, const MTPmessages_Chats &result) {
	_peerRequests.remove(peer);
	
	if (result.type() == mtpc_messages_chats) {
		const QVector<MTPChat> &v(result.c_messages_chats().vchats.c_vector().v);
		bool badVersion = false;
		if (peer->isChat()) {
			badVersion = (!v.isEmpty() && v.at(0).type() == mtpc_chat && v.at(0).c_chat().vversion.v < peer->asChat()->version);
		} else if (peer->isChannel()) {
			badVersion = (!v.isEmpty() && v.at(0).type() == mtpc_channel && v.at(0).c_chat().vversion.v < peer->asChannel()->version);
		}
		PeerData *chat = App::feedChats(result.c_messages_chats().vchats);
		if (chat == peer) {
			if (badVersion) {
				if (peer->isChat()) {
					peer->asChat()->version = v.at(0).c_chat().vversion.v;
				} else if (peer->isChannel()) {
					peer->asChannel()->version = v.at(0).c_channel().vversion.v;
				}
				requestPeer(peer);
			}
		}
	}
}

void ApiWrap::gotUser(PeerData *peer, const MTPVector<MTPUser> &result) {
	_peerRequests.remove(peer);

	UserData *user = App::feedUsers(result);
	if (user == peer) {
	}
}

void ApiWrap::gotChats(const MTPmessages_Chats &result) {
	App::feedChats(result.c_messages_chats().vchats);
}

void ApiWrap::gotUsers(const MTPVector<MTPUser> &result) {
	App::feedUsers(result);
}

bool ApiWrap::gotPeerFailed(PeerData *peer, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_peerRequests.remove(peer);
	return true;
}

void ApiWrap::requestSelfParticipant(ChannelData *channel) {
	if (_selfParticipantRequests.contains(channel)) return;
	_selfParticipantRequests.insert(channel, MTP::send(MTPchannels_GetParticipant(channel->inputChannel, MTP_inputUserSelf()), rpcDone(&ApiWrap::gotSelfParticipant, channel), rpcFail(&ApiWrap::gotSelfParticipantFail, channel), 0, 5));
}

void ApiWrap::gotSelfParticipant(ChannelData *channel, const MTPchannels_ChannelParticipant &result) {
	_selfParticipantRequests.remove(channel);
	if (result.type() != mtpc_channels_channelParticipant) {
		LOG(("API Error: unknown type in gotSelfParticipant (%1)").arg(result.type()));
		channel->inviter = -1;
		if (App::main()) App::main()->onSelfParticipantUpdated(channel);
		return;
	}

	const MTPDchannels_channelParticipant &p(result.c_channels_channelParticipant());
	App::feedUsers(p.vusers);

	switch (p.vparticipant.type()) {
	case mtpc_channelParticipantSelf: {
		const MTPDchannelParticipantSelf &d(p.vparticipant.c_channelParticipantSelf());
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;
	case mtpc_channelParticipantCreator: {
		const MTPDchannelParticipantCreator &d(p.vparticipant.c_channelParticipantCreator());
		channel->inviter = MTP::authedId();
		channel->inviteDate = date(MTP_int(channel->date));
	} break;
	case mtpc_channelParticipantModerator: {
		const MTPDchannelParticipantModerator &d(p.vparticipant.c_channelParticipantModerator());
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;
	case mtpc_channelParticipantEditor: {
		const MTPDchannelParticipantEditor &d(p.vparticipant.c_channelParticipantEditor());
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;

	}

	if (App::main()) App::main()->onSelfParticipantUpdated(channel);
}

bool ApiWrap::gotSelfParticipantFail(ChannelData *channel, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	if (error.type() == qstr("USER_NOT_PARTICIPANT")) {
		channel->inviter = -1;
	}
	_selfParticipantRequests.remove(channel);
	return true;
}

void ApiWrap::scheduleStickerSetRequest(uint64 setId, uint64 access) {
	if (!_stickerSetRequests.contains(setId)) {
		_stickerSetRequests.insert(setId, qMakePair(access, 0));
	}
}

void ApiWrap::requestStickerSets() {
	for (QMap<uint64, QPair<uint64, mtpRequestId> >::iterator i = _stickerSetRequests.begin(), j = i, e = _stickerSetRequests.end(); i != e; i = j) {
		if (i.value().second) continue;

		++j;
		int32 wait = (j == e) ? 0 : 10;
		i.value().second = MTP::send(MTPmessages_GetStickerSet(MTP_inputStickerSetID(MTP_long(i.key()), MTP_long(i.value().first))), rpcDone(&ApiWrap::gotStickerSet, i.key()), rpcFail(&ApiWrap::gotStickerSetFail, i.key()), 0, wait);
	}
}

void ApiWrap::gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result) {
	_stickerSetRequests.remove(setId);
	
	if (result.type() != mtpc_messages_stickerSet) return;
	const MTPDmessages_stickerSet &d(result.c_messages_stickerSet());
	
	if (d.vset.type() != mtpc_stickerSet) return;
	const MTPDstickerSet &s(d.vset.c_stickerSet());

	StickerSets &sets(cRefStickerSets());
	StickerSets::iterator it = sets.find(setId);
	if (it == sets.cend()) return;

	it->access = s.vaccess_hash.v;
	it->hash = s.vhash.v;
	it->shortName = qs(s.vshort_name);
	QString title = qs(s.vtitle);
	if ((it->flags & MTPDstickerSet_flag_official) && !title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
		title = lang(lng_stickers_default_set);
	}
	it->title = title;
	it->flags = s.vflags.v;

	const QVector<MTPDocument> &d_docs(d.vdocuments.c_vector().v);
	StickerSets::iterator custom = sets.find(CustomStickerSetId);

	QSet<DocumentData*> found;
	int32 wasCount = -1;
	for (int32 i = 0, l = d_docs.size(); i != l; ++i) {
		DocumentData *doc = App::feedDocument(d_docs.at(i));
		if (!doc || !doc->sticker()) continue;

		if (wasCount < 0) wasCount = it->stickers.size();
		if (it->stickers.indexOf(doc) < 0) {
			it->stickers.push_back(doc);
		} else {
			found.insert(doc);
		}

		if (custom != sets.cend()) {
			int32 index = custom->stickers.indexOf(doc);
			if (index >= 0) {
				custom->stickers.removeAt(index);
			}
		}
	}
	if (custom != sets.cend() && custom->stickers.isEmpty()) {
		sets.erase(custom);
		custom = sets.end();
	}

	bool writeRecent = false;
	RecentStickerPack &recent(cGetRecentStickers());

	if (wasCount < 0) { // no stickers received
		for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}
		cRefStickerSetsOrder().removeOne(setId);
		sets.erase(it);
	} else {
		for (int32 j = 0, l = wasCount; j < l;) {
			if (found.contains(it->stickers.at(j))) {
				++j;
			} else {
				for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
					if (it->stickers.at(j) == i->first) {
						i = recent.erase(i);
						writeRecent = true;
					} else {
						++i;
					}
				}
				it->stickers.removeAt(j);
				--l;
			}
		}
		if (it->stickers.isEmpty()) {
			cRefStickerSetsOrder().removeOne(setId);
			sets.erase(it);
		}
	}

	if (writeRecent) {
		Local::writeUserSettings();
	}

	Local::writeStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool ApiWrap::gotStickerSetFail(uint64 setId, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_stickerSetRequests.remove(setId);
	return true;
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

void ApiWrap::resolveWebPages() {
	MessageIds ids; // temp_req_id = -1
	typedef QPair<int32, MessageIds> IndexAndMessageIds;
	typedef QMap<ChannelData*, IndexAndMessageIds> MessageIdsByChannel;
	MessageIdsByChannel idsByChannel; // temp_req_id = -index - 2

	const WebPageItems &items(App::webPageItems());
	ids.reserve(_webPagesPending.size());
	int32 t = unixtime(), m = INT_MAX;
	for (WebPagesPending::iterator i = _webPagesPending.begin(); i != _webPagesPending.cend(); ++i) {
		if (i.value() > 0) continue;
		if (i.key()->pendingTill <= t) {
			WebPageItems::const_iterator j = items.constFind(i.key());
			if (j != items.cend() && !j.value().isEmpty()) {
				for (HistoryItemsMap::const_iterator it = j.value().cbegin(); it != j.value().cend(); ++it) {
					HistoryItem *item = j.value().begin().key();
					if (item->id > 0) {
						if (item->channelId() == NoChannel) {
							ids.push_back(MTP_int(item->id));
							i.value() = -1;
						} else {
							ChannelData *channel = item->history()->peer->asChannel();
							MessageIdsByChannel::iterator channelMap = idsByChannel.find(channel);
							if (channelMap == idsByChannel.cend()) {
								channelMap = idsByChannel.insert(channel, IndexAndMessageIds(idsByChannel.size(), MessageIds(1, MTP_int(item->id))));
							} else {
								channelMap.value().second.push_back(MTP_int(item->id));
							}
							i.value() = -channelMap.value().first - 2;
						}
						break;
					}
				}
			}
		} else {
			m = qMin(m, i.key()->pendingTill - t);
		}
	}

	mtpRequestId req = ids.isEmpty() ? 0 : MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotWebPages, (ChannelData*)0), RPCFailHandlerPtr(), 0, 5);
	typedef QVector<mtpRequestId> RequestIds;
	RequestIds reqsByIndex(idsByChannel.size(), 0);
	for (MessageIdsByChannel::const_iterator i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i.value().first] = MTP::send(MTPchannels_GetMessages(i.key()->inputChannel, MTP_vector<MTPint>(i.value().second)), rpcDone(&ApiWrap::gotWebPages, i.key()), RPCFailHandlerPtr(), 0, 5);
	}
	if (req || !reqsByIndex.isEmpty()) {
		for (WebPagesPending::iterator i = _webPagesPending.begin(); i != _webPagesPending.cend(); ++i) {
			if (i.value() > 0) continue;
			if (i.value() < 0) {
				if (i.value() == -1) {
					i.value() = req;
				} else {
					i.value() = reqsByIndex[-i.value() - 2];
				}
			}
		}
	}

	if (m < INT_MAX) _webPagesTimer.start(m * 1000);
}

void ApiWrap::gotWebPages(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(msgs.c_messages_channelMessages());
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotWebPages)"));
		}
		if (d.has_collapsed()) { // should not be returned
			LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (ApiWrap::gotWebPages)"));
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;
	}

	if (!v) return;
	QMap<uint64, int32> msgsIds; // copied from feedMsgs
	for (int32 i = 0, l = v->size(); i < l; ++i) {
		const MTPMessage &msg(v->at(i));
		switch (msg.type()) {
		case mtpc_message: msgsIds.insert((uint64(uint32(msg.c_message().vid.v)) << 32) | uint64(i), i); break;
		case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i); break;
		case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i); break;
		}
	}

	MainWidget *m = App::main();
	for (QMap<uint64, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addNewMessage(v->at(i.value()), NewMessageExisting);
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
