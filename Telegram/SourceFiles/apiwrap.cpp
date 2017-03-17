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
#include "apiwrap.h"

#include "data/data_drafts.h"
#include "observer_peer.h"
#include "lang.h"
#include "application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "historywidget.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "boxes/confirmbox.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"

ApiWrap::ApiWrap(QObject *parent) : QObject(parent)
, _messageDataResolveDelayed(new SingleDelayedCall(this, "resolveMessageDatas")) {
	Window::Theme::Background()->start();

	connect(&_webPagesTimer, SIGNAL(timeout()), this, SLOT(resolveWebPages()));
	connect(&_draftsSaveTimer, SIGNAL(timeout()), this, SLOT(saveDraftsToCloud()));
}

void ApiWrap::init() {
}

void ApiWrap::requestMessageData(ChannelData *channel, MsgId msgId, RequestMessageDataCallback callback) {
	MessageDataRequest &req(channel ? _channelMessageDataRequests[channel][msgId] : _messageDataRequests[msgId]);
	if (callback) {
		req.callbacks.append(callback);
	}
	if (!req.req) _messageDataResolveDelayed->call();
}

ApiWrap::MessageIds ApiWrap::collectMessageIds(const MessageDataRequests &requests) {
	MessageIds result;
	result.reserve(requests.size());
	for (auto i = requests.cbegin(), e = requests.cend(); i != e; ++i) {
		if (i.value().req > 0) continue;
		result.push_back(MTP_int(i.key()));
	}
	return result;
}

ApiWrap::MessageDataRequests *ApiWrap::messageDataRequests(ChannelData *channel, bool onlyExisting) {
	if (channel) {
		auto i = _channelMessageDataRequests.find(channel);
		if (i == _channelMessageDataRequests.cend()) {
			if (onlyExisting) return 0;
			i = _channelMessageDataRequests.insert(channel, MessageDataRequests());
		}
		return &i.value();
	}
	return &_messageDataRequests;
}

void ApiWrap::resolveMessageDatas() {
	if (_messageDataRequests.isEmpty() && _channelMessageDataRequests.isEmpty()) return;

	MessageIds ids = collectMessageIds(_messageDataRequests);
	if (!ids.isEmpty()) {
		mtpRequestId req = MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotMessageDatas, (ChannelData*)nullptr), RPCFailHandlerPtr(), 0, 5);
		for (auto &request : _messageDataRequests) {
			if (request.req > 0) continue;
			request.req = req;
		}
	}
	for (auto j = _channelMessageDataRequests.begin(); j != _channelMessageDataRequests.cend();) {
		if (j->isEmpty()) {
			j = _channelMessageDataRequests.erase(j);
			continue;
		}
		MessageIds ids = collectMessageIds(j.value());
		if (!ids.isEmpty()) {
			mtpRequestId req = MTP::send(MTPchannels_GetMessages(j.key()->inputChannel, MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotMessageDatas, j.key()), RPCFailHandlerPtr(), 0, 5);
			for (auto &request : *j) {
				if (request.req > 0) continue;
				request.req = req;
			}
		}
		++j;
	}
}

void ApiWrap::updatesReceived(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);
}

void ApiWrap::gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		auto &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;

	case mtpc_messages_channelMessages: {
		auto &d(msgs.c_messages_channelMessages());
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("App Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotDependencyItem)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageExisting);
	} break;
	}
	MessageDataRequests *requests(messageDataRequests(channel, true));
	if (requests) {
		for (auto i = requests->begin(); i != requests->cend();) {
			if (i.value().req == req) {
				for_const (auto &callback, i.value().callbacks) {
					callback(channel, i.key());
				}
				i = requests->erase(i);
			} else {
				++i;
			}
		}
		if (channel && requests->isEmpty()) {
			_channelMessageDataRequests.remove(channel);
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

void ApiWrap::processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result) {
	gotChatFull(peer, result, 0);
}

void ApiWrap::processFullPeer(PeerData *peer, const MTPUserFull &result) {
	gotUserFull(peer, result, 0);
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req) {
	auto &d = result.c_messages_chatFull();
	auto &vc = d.vchats.v;
	auto badVersion = false;
	if (peer->isChat()) {
		badVersion = (!vc.isEmpty() && vc.at(0).type() == mtpc_chat && vc.at(0).c_chat().vversion.v < peer->asChat()->version);
	} else if (peer->isChannel()) {
		badVersion = (!vc.isEmpty() && vc.at(0).type() == mtpc_channel && vc.at(0).c_channel().vversion.v < peer->asChannel()->version);
	}

	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);

	if (peer->isChat()) {
		if (d.vfull_chat.type() != mtpc_chatFull) {
			LOG(("MTP Error: bad type in gotChatFull for chat: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_chatFull();
		App::feedParticipants(f.vparticipants, false, false);
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
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
		chat->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	} else if (peer->isChannel()) {
		if (d.vfull_chat.type() != mtpc_channelFull) {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f(d.vfull_chat.c_channelFull());
		PhotoData *photo = App::feedPhoto(f.vchat_photo);
		ChannelData *channel = peer->asChannel();

		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();

		channel->flagsFull = f.vflags.v;
		if (photo) {
			channel->photoId = photo->id;
			photo->peer = channel;
		} else {
			channel->photoId = 0;
		}
		if (f.has_migrated_from_chat_id()) {
			if (!channel->mgInfo) {
				channel->flags |= MTPDchannel::Flag::f_megagroup;
				channel->flagsUpdated();
			}
			ChatData *cfrom = App::chat(peerFromChat(f.vmigrated_from_chat_id));
			bool updatedTo = (cfrom->migrateToPtr != channel), updatedFrom = (channel->mgInfo->migrateFromPtr != cfrom);
			if (updatedTo) {
				cfrom->migrateToPtr = channel;
			}
			if (updatedFrom) {
				channel->mgInfo->migrateFromPtr = cfrom;
				if (History *h = App::historyLoaded(cfrom->id)) {
					if (History *hto = App::historyLoaded(channel->id)) {
						if (!h->isEmpty()) {
							h->clear(true);
						}
						if (hto->inChatList(Dialogs::Mode::All) && h->inChatList(Dialogs::Mode::All)) {
							App::removeDialog(h);
						}
					}
				}
				Notify::migrateUpdated(channel);
			}
			if (updatedTo) {
				Notify::migrateUpdated(cfrom);
				App::main()->peerUpdated(cfrom);
			}
		}
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
					App::clearPeerUpdated(user);
					emit fullPeerUpdated(user);
				}
			} break;
			}
		}
		channel->setAbout(qs(f.vabout));
		channel->setMembersCount(f.has_participants_count() ? f.vparticipants_count.v : 0);
		channel->setAdminsCount(f.has_admins_count() ? f.vadmins_count.v : 0);
		channel->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		if (History *h = App::historyLoaded(channel->id)) {
			if (h->inboxReadBefore < f.vread_inbox_max_id.v + 1) {
				h->setUnreadCount(f.vunread_count.v);
				h->inboxReadBefore = f.vread_inbox_max_id.v + 1;
			}
			accumulate_max(h->outboxReadBefore, f.vread_outbox_max_id.v + 1);
		}
		if (channel->isMegagroup()) {
			if (f.has_pinned_msg_id()) {
				channel->mgInfo->pinnedMsgId = f.vpinned_msg_id.v;
			} else {
				channel->mgInfo->pinnedMsgId = 0;
			}
		}
		channel->fullUpdated();

		if (canViewAdmins != channel->canViewAdmins()) Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelCanViewAdmins);
		if (canViewMembers != channel->canViewMembers()) Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelCanViewMembers);

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	}

	if (req) {
		QMap<PeerData*, mtpRequestId>::iterator i = _fullPeerRequests.find(peer);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
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
}

void ApiWrap::gotUserFull(PeerData *peer, const MTPUserFull &result, mtpRequestId req) {
	auto user = peer->asUser();
	t_assert(user != nullptr);

	auto &d = result.c_userFull();
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	if (d.has_profile_photo()) {
		App::feedPhoto(d.vprofile_photo);
	}
	App::feedUserLink(MTP_int(peerToUser(peer->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
	if (App::main()) {
		notifySettingReceived(MTP_inputNotifyPeer(peer->input), d.vnotify_settings);
	}

	if (d.has_bot_info()) {
		user->setBotInfo(d.vbot_info);
	} else {
		user->setBotInfoVersion(-1);
	}
	user->setBlockStatus(d.is_blocked() ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
	user->setAbout(d.has_about() ? qs(d.vabout) : QString());
	user->setCommonChatsCount(d.vcommon_chats_count.v);

	if (req) {
		QMap<PeerData*, mtpRequestId>::iterator i = _fullPeerRequests.find(peer);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	App::clearPeerUpdated(peer);
	emit fullPeerUpdated(peer);
}

bool ApiWrap::gotPeerFullFailed(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

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

void ApiWrap::requestLastParticipants(ChannelData *peer, bool fromStart) {
	if (!peer || !peer->isMegagroup()) return;
	bool needAdmins = peer->amEditor(), adminsOutdated = (peer->mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsAdminsOutdated);
	if ((needAdmins && adminsOutdated) || peer->lastParticipantsCountOutdated()) {
		fromStart = true;
	}
	auto i = _participantsRequests.find(peer);
	if (i != _participantsRequests.cend()) {
		if (fromStart && i.value() < 0) { // was not loading from start
			_participantsRequests.erase(i);
		} else {
			return;
		}
	}
	mtpRequestId req = MTP::send(MTPchannels_GetParticipants(peer->inputChannel, MTP_channelParticipantsRecent(), MTP_int(fromStart ? 0 : peer->mgInfo->lastParticipants.size()), MTP_int(Global::ChatSizeMax())), rpcDone(&ApiWrap::lastParticipantsDone, peer), rpcFail(&ApiWrap::lastParticipantsFail, peer));
	_participantsRequests.insert(peer, fromStart ? req : -req);
}

void ApiWrap::requestBots(ChannelData *peer) {
	if (!peer || !peer->isMegagroup() || _botsRequests.contains(peer)) return;
	_botsRequests.insert(peer, MTP::send(MTPchannels_GetParticipants(peer->inputChannel, MTP_channelParticipantsBots(), MTP_int(0), MTP_int(Global::ChatSizeMax())), rpcDone(&ApiWrap::lastParticipantsDone, peer), rpcFail(&ApiWrap::lastParticipantsFail, peer)));
}

void ApiWrap::gotChat(PeerData *peer, const MTPmessages_Chats &result) {
	_peerRequests.remove(peer);

	if (auto chats = Api::getChatsFromMessagesChats(result)) {
		auto &v = chats->v;
		bool badVersion = false;
		if (peer->isChat()) {
			badVersion = (!v.isEmpty() && v.at(0).type() == mtpc_chat && v.at(0).c_chat().vversion.v < peer->asChat()->version);
		} else if (peer->isChannel()) {
			badVersion = (!v.isEmpty() && v.at(0).type() == mtpc_channel && v.at(0).c_chat().vversion.v < peer->asChannel()->version);
		}
		auto chat = App::feedChats(*chats);
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
	if (auto chats = Api::getChatsFromMessagesChats(result)) {
		App::feedChats(*chats);
	}
}

void ApiWrap::gotUsers(const MTPVector<MTPUser> &result) {
	App::feedUsers(result);
}

bool ApiWrap::gotPeerFailed(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_peerRequests.remove(peer);
	return true;
}

void ApiWrap::lastParticipantsDone(ChannelData *peer, const MTPchannels_ChannelParticipants &result, mtpRequestId req) {
	bool bots = (_botsRequests.value(peer) == req), fromStart = false;
	if (bots) {
		_botsRequests.remove(peer);
	} else {
		int32 was = _participantsRequests.value(peer);
		if (was == req) {
			fromStart = true;
		} else if (was != -req) {
			return;
		}
		_participantsRequests.remove(peer);
	}

	if (!peer->mgInfo || result.type() != mtpc_channels_channelParticipants) return;

	History *h = 0;
	if (bots) {
		h = App::historyLoaded(peer->id);
		peer->mgInfo->bots.clear();
		peer->mgInfo->botStatus = -1;
	} else if (fromStart) {
		peer->mgInfo->lastAdmins.clear();
		peer->mgInfo->lastParticipants.clear();
		peer->mgInfo->lastParticipantsStatus = MegagroupInfo::LastParticipantsUpToDate;
	}

	auto &d = result.c_channels_channelParticipants();
	auto &v = d.vparticipants.v;
	App::feedUsers(d.vusers);
	bool added = false, needBotsInfos = false;
	int32 botStatus = peer->mgInfo->botStatus;
	bool keyboardBotFound = !h || !h->lastKeyboardFrom;
	for (QVector<MTPChannelParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
		int32 userId = 0;
		bool admin = false;

		switch (i->type()) {
		case mtpc_channelParticipant: userId = i->c_channelParticipant().vuser_id.v; break;
		case mtpc_channelParticipantSelf: userId = i->c_channelParticipantSelf().vuser_id.v; break;
		case mtpc_channelParticipantModerator: userId = i->c_channelParticipantModerator().vuser_id.v; break;
		case mtpc_channelParticipantEditor: userId = i->c_channelParticipantEditor().vuser_id.v; admin = true; break;
		case mtpc_channelParticipantKicked: userId = i->c_channelParticipantKicked().vuser_id.v; break;
		case mtpc_channelParticipantCreator: userId = i->c_channelParticipantCreator().vuser_id.v; admin = true; break;
		}
		UserData *u = App::user(userId);
		if (bots) {
			if (u->botInfo) {
				peer->mgInfo->bots.insert(u);
				botStatus = 2;// (botStatus > 0/* || !i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
				if (!u->botInfo->inited) {
					needBotsInfos = true;
				}
			}
			if (!keyboardBotFound && u->id == h->lastKeyboardFrom) {
				keyboardBotFound = true;
			}
		} else {
			if (peer->mgInfo->lastParticipants.indexOf(u) < 0) {
				peer->mgInfo->lastParticipants.push_back(u);
				if (admin) peer->mgInfo->lastAdmins.insert(u);
				if (u->botInfo) {
					peer->mgInfo->bots.insert(u);
					if (peer->mgInfo->botStatus != 0 && peer->mgInfo->botStatus < 2) {
						peer->mgInfo->botStatus = 2;
					}
				}
				added = true;
			}
		}
	}
	if (needBotsInfos) {
		requestFullPeer(peer);
	}
	if (!keyboardBotFound) {
		h->clearLastKeyboard();
	}
	int newMembersCount = qMax(d.vcount.v, v.count());
	if (newMembersCount > peer->membersCount()) {
		peer->setMembersCount(newMembersCount);
	}
	if (!bots) {
		if (v.isEmpty()) {
			peer->setMembersCount(peer->mgInfo->lastParticipants.size());
		}
		Notify::PeerUpdate update(peer);
		update.flags |= Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged;
		Notify::peerUpdatedDelayed(update);
	}

	peer->mgInfo->botStatus = botStatus;
	if (App::main()) emit fullPeerUpdated(peer);
}

bool ApiWrap::lastParticipantsFail(ChannelData *peer, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	if (_participantsRequests.value(peer) == req || _participantsRequests.value(peer) == -req) {
		_participantsRequests.remove(peer);
	} else if (_botsRequests.value(peer) == req) {
		_botsRequests.remove(peer);
	}
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

	auto &p = result.c_channels_channelParticipant();
	App::feedUsers(p.vusers);

	switch (p.vparticipant.type()) {
	case mtpc_channelParticipantSelf: {
		auto &d = p.vparticipant.c_channelParticipantSelf();
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;
	case mtpc_channelParticipantCreator: {
		auto &d = p.vparticipant.c_channelParticipantCreator();
		channel->inviter = AuthSession::CurrentUserId();
		channel->inviteDate = date(MTP_int(channel->date));
	} break;
	case mtpc_channelParticipantModerator: {
		auto &d = p.vparticipant.c_channelParticipantModerator();
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;
	case mtpc_channelParticipantEditor: {
		auto &d = p.vparticipant.c_channelParticipantEditor();
		channel->inviter = d.vinviter_id.v;
		channel->inviteDate = date(d.vdate);
	} break;

	}

	if (App::main()) App::main()->onSelfParticipantUpdated(channel);
}

bool ApiWrap::gotSelfParticipantFail(ChannelData *channel, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("USER_NOT_PARTICIPANT")) {
		channel->inviter = -1;
	}
	_selfParticipantRequests.remove(channel);
	return true;
}

void ApiWrap::kickParticipant(PeerData *peer, UserData *user) {
	KickRequest req(peer, user);
	if (_kickRequests.contains(req)) return;

	if (peer->isChannel()) {
		_kickRequests.insert(req, MTP::send(MTPchannels_KickFromChannel(peer->asChannel()->inputChannel, user->inputUser, MTP_bool(true)), rpcDone(&ApiWrap::kickParticipantDone, req), rpcFail(&ApiWrap::kickParticipantFail, req)));
	}
}

void ApiWrap::kickParticipantDone(KickRequest kick, const MTPUpdates &result, mtpRequestId req) {
	_kickRequests.remove(kick);
	if (kick.first->isMegagroup()) {
		auto channel = kick.first->asChannel();
		auto megagroupInfo = channel->mgInfo;

		int32 i = megagroupInfo->lastParticipants.indexOf(kick.second);
		if (i >= 0) {
			megagroupInfo->lastParticipants.removeAt(i);
		}

		if (channel->membersCount() > 1) {
			channel->setMembersCount(channel->membersCount() - 1);
		} else {
			megagroupInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
			megagroupInfo->lastParticipantsCount = 0;
		}
		if (megagroupInfo->lastAdmins.contains(kick.second)) {
			megagroupInfo->lastAdmins.remove(kick.second);
			if (channel->adminsCount() > 1) {
				channel->setAdminsCount(channel->adminsCount() - 1);
			}
			Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::AdminsChanged);
		}
		megagroupInfo->bots.remove(kick.second);
		if (megagroupInfo->bots.isEmpty() && megagroupInfo->botStatus > 0) {
			megagroupInfo->botStatus = -1;
		}
	}
	Notify::peerUpdatedDelayed(kick.first, Notify::PeerUpdate::Flag::MembersChanged);
	emit fullPeerUpdated(kick.first);
}

bool ApiWrap::kickParticipantFail(KickRequest kick, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	_kickRequests.remove(kick);
	return true;
}

void ApiWrap::scheduleStickerSetRequest(uint64 setId, uint64 access) {
	if (!_stickerSetRequests.contains(setId)) {
		_stickerSetRequests.insert(setId, qMakePair(access, 0));
	}
}

void ApiWrap::requestStickerSets() {
	for (QMap<uint64, QPair<uint64, mtpRequestId> >::iterator i = _stickerSetRequests.begin(), j = i, e = _stickerSetRequests.end(); i != e; i = j) {
		++j;
		if (i.value().second) continue;

		int32 wait = (j == e) ? 0 : 10;
		i.value().second = MTP::send(MTPmessages_GetStickerSet(MTP_inputStickerSetID(MTP_long(i.key()), MTP_long(i.value().first))), rpcDone(&ApiWrap::gotStickerSet, i.key()), rpcFail(&ApiWrap::gotStickerSetFail, i.key()), 0, wait);
	}
}

void ApiWrap::saveStickerSets(const Stickers::Order &localOrder, const Stickers::Order &localRemoved) {
	for (auto requestId : base::take(_stickerSetDisenableRequests)) {
		MTP::cancel(requestId);
	}
	MTP::cancel(base::take(_stickersReorderRequestId));
	MTP::cancel(base::take(_stickersClearRecentRequestId));

	auto writeInstalled = true, writeRecent = false, writeCloudRecent = false, writeArchived = false;
	auto &recent = cGetRecentStickers();
	auto &sets = Global::RefStickerSets();

	_stickersOrder = localOrder;
	for_const (auto removedSetId, localRemoved) {
		if (removedSetId == Stickers::CloudRecentSetId) {
			if (sets.remove(Stickers::CloudRecentSetId) != 0) {
				writeCloudRecent = true;
			}
			if (sets.remove(Stickers::CustomSetId)) {
				writeInstalled = true;
			}
			if (!recent.isEmpty()) {
				recent.clear();
				writeRecent = true;
			}

			MTPmessages_ClearRecentStickers::Flags flags = 0;
			_stickersClearRecentRequestId = MTP::send(MTPmessages_ClearRecentStickers(MTP_flags(flags)), rpcDone(&ApiWrap::stickersClearRecentDone), rpcFail(&ApiWrap::stickersClearRecentFail));
			continue;
		}

		auto it = sets.find(removedSetId);
		if (it != sets.cend()) {
			for (auto i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			if (!(it->flags & MTPDstickerSet::Flag::f_archived)) {
				MTPInputStickerSet setId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));
				_stickerSetDisenableRequests.insert(MTP::send(MTPmessages_UninstallStickerSet(setId), rpcDone(&ApiWrap::stickerSetDisenableDone), rpcFail(&ApiWrap::stickerSetDisenableFail), 0, 5));
				int removeIndex = Global::StickerSetsOrder().indexOf(it->id);
				if (removeIndex >= 0) Global::RefStickerSetsOrder().removeAt(removeIndex);
				if (!(it->flags & MTPDstickerSet_ClientFlag::f_featured) && !(it->flags & MTPDstickerSet_ClientFlag::f_special)) {
					sets.erase(it);
				} else {
					if (it->flags & MTPDstickerSet::Flag::f_archived) {
						writeArchived = true;
					}
					it->flags &= ~(MTPDstickerSet::Flag::f_installed | MTPDstickerSet::Flag::f_archived);
				}
			}
		}
	}

	// Clear all installed flags, set only for sets from order.
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			set.flags &= ~MTPDstickerSet::Flag::f_installed;
		}
	}

	auto &order(Global::RefStickerSetsOrder());
	order.clear();
	for_const (auto setId, _stickersOrder) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			if ((it->flags & MTPDstickerSet::Flag::f_archived) && !localRemoved.contains(it->id)) {
				MTPInputStickerSet mtpSetId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));
				_stickerSetDisenableRequests.insert(MTP::send(MTPmessages_InstallStickerSet(mtpSetId, MTP_boolFalse()), rpcDone(&ApiWrap::stickerSetDisenableDone), rpcFail(&ApiWrap::stickerSetDisenableFail), 0, 5));
				it->flags &= ~MTPDstickerSet::Flag::f_archived;
				writeArchived = true;
			}
			order.push_back(setId);
			it->flags |= MTPDstickerSet::Flag::f_installed;
		}
	}
	for (auto it = sets.begin(); it != sets.cend();) {
		if ((it->flags & MTPDstickerSet_ClientFlag::f_featured)
			|| (it->flags & MTPDstickerSet::Flag::f_installed)
			|| (it->flags & MTPDstickerSet::Flag::f_archived)
			|| (it->flags & MTPDstickerSet_ClientFlag::f_special)) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	if (writeInstalled) Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();
	if (writeArchived) Local::writeArchivedStickers();
	if (writeCloudRecent) Local::writeRecentStickers();
	emit App::main()->stickersUpdated();

	if (_stickerSetDisenableRequests.isEmpty()) {
		stickersSaveOrder();
	} else {
		MTP::sendAnything();
	}
}

void ApiWrap::joinChannel(ChannelData *channel) {
	if (channel->amIn()) {
		channelAmInUpdated(channel);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = MTP::send(MTPchannels_JoinChannel(channel->inputChannel), rpcDone(&ApiWrap::channelAmInDone, channel), rpcFail(&ApiWrap::channelAmInFail, channel));
		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::leaveChannel(ChannelData *channel) {
	if (!channel->amIn()) {
		channelAmInUpdated(channel);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = MTP::send(MTPchannels_LeaveChannel(channel->inputChannel), rpcDone(&ApiWrap::channelAmInDone, channel), rpcFail(&ApiWrap::channelAmInFail, channel));
		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::channelAmInUpdated(ChannelData *channel) {
	Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelAmIn);
}

void ApiWrap::channelAmInDone(ChannelData *channel, const MTPUpdates &updates) {
	_channelAmInRequests.remove(channel);

	updatesReceived(updates);
}

bool ApiWrap::channelAmInFail(ChannelData *channel, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
		Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
	}
	_channelAmInRequests.remove(channel);
	return true;
}

void ApiWrap::blockUser(UserData *user) {
	if (user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (!_blockRequests.contains(user)) {
		auto requestId = MTP::send(MTPcontacts_Block(user->inputUser), rpcDone(&ApiWrap::blockDone, user), rpcFail(&ApiWrap::blockFail, user));
		_blockRequests.insert(user, requestId);
	}
}

void ApiWrap::unblockUser(UserData *user) {
	if (!user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (!_blockRequests.contains(user)) {
		auto requestId = MTP::send(MTPcontacts_Unblock(user->inputUser), rpcDone(&ApiWrap::unblockDone, user), rpcFail(&ApiWrap::blockFail, user));
		_blockRequests.insert(user, requestId);
	}
}

void ApiWrap::blockDone(UserData *user, const MTPBool &result) {
	_blockRequests.remove(user);
	user->setBlockStatus(UserData::BlockStatus::Blocked);
	emit App::main()->peerUpdated(user);
}

void ApiWrap::unblockDone(UserData *user, const MTPBool &result) {
	_blockRequests.remove(user);
	user->setBlockStatus(UserData::BlockStatus::NotBlocked);
	emit App::main()->peerUpdated(user);
}

bool ApiWrap::blockFail(UserData *user, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_blockRequests.remove(user);
	return true;
}

void ApiWrap::exportInviteLink(PeerData *peer) {
	if (_exportInviteRequests.contains(peer)) {
		return;
	}

	mtpRequestId request = 0;
	if (auto chat = peer->asChat()) {
		request = MTP::send(MTPmessages_ExportChatInvite(chat->inputChat), rpcDone(&ApiWrap::exportInviteDone, peer), rpcFail(&ApiWrap::exportInviteFail, peer));
	} else if (auto channel = peer->asChannel()) {
		request = MTP::send(MTPchannels_ExportInvite(channel->inputChannel), rpcDone(&ApiWrap::exportInviteDone, peer), rpcFail(&ApiWrap::exportInviteFail, peer));
	}
	if (request) {
		_exportInviteRequests.insert(peer, request);
	}
}

void ApiWrap::exportInviteDone(PeerData *peer, const MTPExportedChatInvite &result) {
	_exportInviteRequests.remove(peer);
	if (auto chat = peer->asChat()) {
		chat->setInviteLink((result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString());
	} else if (auto channel = peer->asChannel()) {
		channel->setInviteLink((result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString());
	}
}

bool ApiWrap::exportInviteFail(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_exportInviteRequests.remove(peer);
	return true;
}

void ApiWrap::requestNotifySetting(PeerData *peer) {
	if (_notifySettingRequests.contains(peer)) return;

	MTPInputNotifyPeer notifyPeer = MTP_inputNotifyPeer(peer->input);
	auto requestId = MTP::send(MTPaccount_GetNotifySettings(notifyPeer), rpcDone(&ApiWrap::notifySettingDone, notifyPeer), rpcFail(&ApiWrap::notifySettingFail, peer));
	_notifySettingRequests.insert(peer, requestId);
}

void ApiWrap::saveDraftToCloudDelayed(History *history) {
	_draftsSaveRequestIds.insert(history, 0);
	if (!_draftsSaveTimer.isActive()) {
		_draftsSaveTimer.start(SaveCloudDraftTimeout);
	}
}

bool ApiWrap::hasUnsavedDrafts() const {
	return !_draftsSaveRequestIds.isEmpty();
}

void ApiWrap::savePrivacy(const MTPInputPrivacyKey &key, QVector<MTPInputPrivacyRule> &&rules) {
	auto keyTypeId = key.type();
	auto it = _privacySaveRequests.find(keyTypeId);
	if (it != _privacySaveRequests.cend()) {
		MTP::cancel(it.value());
		_privacySaveRequests.erase(it);
	}
	auto requestId = MTP::send(MTPaccount_SetPrivacy(key, MTP_vector<MTPInputPrivacyRule>(std::move(rules))), rpcDone(&ApiWrap::savePrivacyDone, keyTypeId), rpcFail(&ApiWrap::savePrivacyFail, keyTypeId));
	_privacySaveRequests.insert(keyTypeId, requestId);
}

void ApiWrap::savePrivacyDone(mtpTypeId keyTypeId, const MTPaccount_PrivacyRules &result) {
	Expects(result.type() == mtpc_account_privacyRules);
	auto &rules = result.c_account_privacyRules();
	App::feedUsers(rules.vusers);
	_privacySaveRequests.remove(keyTypeId);
	handlePrivacyChange(keyTypeId, rules.vrules);
}

bool ApiWrap::savePrivacyFail(mtpTypeId keyTypeId, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	_privacySaveRequests.remove(keyTypeId);
	return true;
}

void ApiWrap::handlePrivacyChange(mtpTypeId keyTypeId, const MTPVector<MTPPrivacyRule> &rules) {
	if (keyTypeId == mtpc_privacyKeyStatusTimestamp) {
		enum class Rule {
			Unknown,
			Allow,
			Disallow,
		};
		auto userRules = QMap<UserId, Rule>();
		auto contactsRule = Rule::Unknown;
		auto everyoneRule = Rule::Unknown;
		for (auto &rule : rules.v) {
			auto type = rule.type();
			if (type != mtpc_privacyValueAllowAll && type != mtpc_privacyValueDisallowAll && contactsRule != Rule::Unknown) {
				// This is simplified: we ignore per-user rules that come after a contacts rule.
				// But none of the official apps provide such complicated rule sets, so its fine.
				continue;
			}

			switch (type) {
			case mtpc_privacyValueAllowAll: everyoneRule = Rule::Allow; break;
			case mtpc_privacyValueDisallowAll: everyoneRule = Rule::Disallow; break;
			case mtpc_privacyValueAllowContacts: contactsRule = Rule::Allow; break;
			case mtpc_privacyValueDisallowContacts: contactsRule = Rule::Disallow; break;
			case mtpc_privacyValueAllowUsers: {
				for_const (auto &userId, rule.c_privacyValueAllowUsers().vusers.v) {
					if (!userRules.contains(userId.v)) {
						userRules.insert(userId.v, Rule::Allow);
					}
				}
			} break;
			case mtpc_privacyValueDisallowUsers: {
				for_const (auto &userId, rule.c_privacyValueDisallowUsers().vusers.v) {
					if (!userRules.contains(userId.v)) {
						userRules.insert(userId.v, Rule::Disallow);
					}
				}
			} break;
			}
			if (everyoneRule != Rule::Unknown) {
				break;
			}
		}

		auto now = unixtime();
		App::enumerateUsers([&userRules, contactsRule, everyoneRule, now](UserData *user) {
			if (user->isSelf() || user->loadedStatus != PeerData::FullLoaded) {
				return;
			}
			if (user->onlineTill <= 0) {
				return;
			}

			if (user->onlineTill + 3 * 86400 >= now) {
				user->onlineTill = -2; // recently
			} else if (user->onlineTill + 7 * 86400 >= now) {
				user->onlineTill = -3; // last week
			} else if (user->onlineTill + 30 * 86400 >= now) {
				user->onlineTill = -4; // last month
			} else {
				user->onlineTill = 0;
			}
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
		});

		if (_contactsStatusesRequestId) {
			MTP::cancel(_contactsStatusesRequestId);
		}
		_contactsStatusesRequestId = MTP::send(MTPcontacts_GetStatuses(), rpcDone(&ApiWrap::contactsStatusesDone), rpcFail(&ApiWrap::contactsStatusesFail));
	}
}

int ApiWrap::onlineTillFromStatus(const MTPUserStatus &status, int currentOnlineTill) {
	switch (status.type()) {
	case mtpc_userStatusEmpty: return 0;
	case mtpc_userStatusRecently: return (currentOnlineTill > -10) ? -2 : currentOnlineTill; // don't modify pseudo-online
	case mtpc_userStatusLastWeek: return -3;
	case mtpc_userStatusLastMonth: return -4;
	case mtpc_userStatusOffline: return status.c_userStatusOffline().vwas_online.v;
	case mtpc_userStatusOnline: return status.c_userStatusOnline().vexpires.v;
	}
	Unexpected("Bad UserStatus type.");
}

void ApiWrap::contactsStatusesDone(const MTPVector<MTPContactStatus> &result) {
	_contactsStatusesRequestId = 0;
	for_const (auto &item, result.v) {
		t_assert(item.type() == mtpc_contactStatus);
		auto &data = item.c_contactStatus();
		if (auto user = App::userLoaded(data.vuser_id.v)) {
			auto oldOnlineTill = user->onlineTill;
			auto newOnlineTill = onlineTillFromStatus(data.vstatus, oldOnlineTill);
			if (oldOnlineTill != newOnlineTill) {
				user->onlineTill = newOnlineTill;
				Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
			}
		}
	}
}

bool ApiWrap::contactsStatusesFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	_contactsStatusesRequestId = 0;
	return true;
}

void ApiWrap::saveDraftsToCloud() {
	for (auto i = _draftsSaveRequestIds.begin(), e = _draftsSaveRequestIds.end(); i != e; ++i) {
		if (i.value()) continue; // sent already

		auto history = i.key();
		auto cloudDraft = history->cloudDraft();
		auto localDraft = history->localDraft();
		if (cloudDraft && cloudDraft->saveRequestId) {
			MTP::cancel(cloudDraft->saveRequestId);
		}
		cloudDraft = history->createCloudDraft(localDraft);

		MTPmessages_SaveDraft::Flags flags = 0;
		auto &textWithTags = cloudDraft->textWithTags;
		if (cloudDraft->previewCancelled) {
			flags |= MTPmessages_SaveDraft::Flag::f_no_webpage;
		}
		if (cloudDraft->msgId) {
			flags |= MTPmessages_SaveDraft::Flag::f_reply_to_msg_id;
		}
		if (!textWithTags.tags.isEmpty()) {
			flags |= MTPmessages_SaveDraft::Flag::f_entities;
		}
		auto entities = linksToMTP(entitiesFromTextTags(textWithTags.tags), true);
		cloudDraft->saveRequestId = MTP::send(MTPmessages_SaveDraft(MTP_flags(flags), MTP_int(cloudDraft->msgId), history->peer->input, MTP_string(textWithTags.text), entities), rpcDone(&ApiWrap::saveCloudDraftDone, history), rpcFail(&ApiWrap::saveCloudDraftFail, history));
		i.value() = cloudDraft->saveRequestId;
	}
	if (_draftsSaveRequestIds.isEmpty()) {
		App::allDraftsSaved(); // can quit the application
	}
}

void ApiWrap::saveCloudDraftDone(History *history, const MTPBool &result, mtpRequestId requestId) {
	if (auto cloudDraft = history->cloudDraft()) {
		if (cloudDraft->saveRequestId == requestId) {
			cloudDraft->saveRequestId = 0;
			history->draftSavedToCloud();
		}
	}
	auto i = _draftsSaveRequestIds.find(history);
	if (i != _draftsSaveRequestIds.cend() && i.value() == requestId) {
		_draftsSaveRequestIds.remove(history);
		if (_draftsSaveRequestIds.isEmpty()) {
			App::allDraftsSaved(); // can quit the application
		}
	}
}

bool ApiWrap::saveCloudDraftFail(History *history, const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (auto cloudDraft = history->cloudDraft()) {
		if (cloudDraft->saveRequestId == requestId) {
			history->clearCloudDraft();
		}
	}
	auto i = _draftsSaveRequestIds.find(history);
	if (i != _draftsSaveRequestIds.cend() && i.value() == requestId) {
		_draftsSaveRequestIds.remove(history);
		if (_draftsSaveRequestIds.isEmpty()) {
			App::allDraftsSaved(); // can quit the application
		}
	}
	return true;
}

void ApiWrap::notifySettingDone(MTPInputNotifyPeer notifyPeer, const MTPPeerNotifySettings &result) {
	if (auto requestedPeer = notifySettingReceived(notifyPeer, result)) {
		_notifySettingRequests.remove(requestedPeer);
	}
}

PeerData *ApiWrap::notifySettingReceived(MTPInputNotifyPeer notifyPeer, const MTPPeerNotifySettings &settings) {
	PeerData *requestedPeer = nullptr;
	switch (notifyPeer.type()) {
	case mtpc_inputNotifyAll: App::main()->applyNotifySetting(MTP_notifyAll(), settings); break;
	case mtpc_inputNotifyUsers: App::main()->applyNotifySetting(MTP_notifyUsers(), settings); break;
	case mtpc_inputNotifyChats: App::main()->applyNotifySetting(MTP_notifyChats(), settings); break;
	case mtpc_inputNotifyPeer: {
		auto &peer = notifyPeer.c_inputNotifyPeer().vpeer;
		switch (peer.type()) {
		case mtpc_inputPeerEmpty: App::main()->applyNotifySetting(MTP_notifyPeer(MTP_peerUser(MTP_int(0))), settings); break;
		case mtpc_inputPeerSelf: requestedPeer = App::self(); break;
		case mtpc_inputPeerUser: requestedPeer = App::user(peerFromUser(peer.c_inputPeerUser().vuser_id)); break;
		case mtpc_inputPeerChat: requestedPeer = App::chat(peerFromChat(peer.c_inputPeerChat().vchat_id)); break;
		case mtpc_inputPeerChannel: requestedPeer = App::channel(peerFromChannel(peer.c_inputPeerChannel().vchannel_id)); break;
		}
		if (requestedPeer) {
			App::main()->applyNotifySetting(MTP_notifyPeer(peerToMTP(requestedPeer->id)), settings);
		}
	} break;
	}
	AuthSession::Current().notifications().checkDelayed();
	return requestedPeer;
}

bool ApiWrap::notifySettingFail(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	notifySettingReceived(MTP_inputNotifyPeer(peer->input), MTP_peerNotifySettingsEmpty());
	_notifySettingRequests.remove(peer);
	return true;
}

void ApiWrap::gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result) {
	_stickerSetRequests.remove(setId);

	if (result.type() != mtpc_messages_stickerSet) return;
	auto &d(result.c_messages_stickerSet());

	if (d.vset.type() != mtpc_stickerSet) return;
	auto &s(d.vset.c_stickerSet());

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend()) return;

	it->access = s.vaccess_hash.v;
	it->hash = s.vhash.v;
	it->shortName = qs(s.vshort_name);
	it->title = stickerSetTitle(s);
	auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
	it->flags = s.vflags.v | clientFlags;
	it->flags &= ~MTPDstickerSet_ClientFlag::f_not_loaded;

	auto &d_docs = d.vdocuments.v;
	auto custom = sets.find(Stickers::CustomSetId);

	StickerPack pack;
	pack.reserve(d_docs.size());
	for (int32 i = 0, l = d_docs.size(); i != l; ++i) {
		DocumentData *doc = App::feedDocument(d_docs.at(i));
		if (!doc || !doc->sticker()) continue;

		pack.push_back(doc);
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
	for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
		if (it->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
			i = recent.erase(i);
			writeRecent = true;
		} else {
			++i;
		}
	}

	if (pack.isEmpty()) {
		int removeIndex = Global::StickerSetsOrder().indexOf(setId);
		if (removeIndex >= 0) Global::RefStickerSetsOrder().removeAt(removeIndex);
		sets.erase(it);
	} else {
		it->stickers = pack;
		it->emoji.clear();
		auto &v = d.vpacks.v;
		for (auto i = 0, l = v.size(); i != l; ++i) {
			if (v[i].type() != mtpc_stickerPack) continue;

			auto &pack = v[i].c_stickerPack();
			if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon))) {
				emoji = emoji->original();
				auto &stickers = pack.vdocuments.v;

				StickerPack p;
				p.reserve(stickers.size());
				for (auto j = 0, c = stickers.size(); j != c; ++j) {
					auto doc = App::document(stickers[j].v);
					if (!doc || !doc->sticker()) continue;

					p.push_back(doc);
				}
				it->emoji.insert(emoji, p);
			}
		}
	}

	if (writeRecent) {
		Local::writeUserSettings();
	}

	if (it->flags & MTPDstickerSet::Flag::f_installed) {
		if (!(it->flags & MTPDstickerSet::Flag::f_archived)) {
			Local::writeInstalledStickers();
		}
	}
	if (it->flags & MTPDstickerSet_ClientFlag::f_featured) {
		Local::writeFeaturedStickers();
	}

	if (App::main()) emit App::main()->stickersUpdated();
}

bool ApiWrap::gotStickerSetFail(uint64 setId, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

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
	using IndexAndMessageIds = QPair<int32, MessageIds>;
	using MessageIdsByChannel = QMap<ChannelData*, IndexAndMessageIds>;
	MessageIdsByChannel idsByChannel; // temp_req_id = -index - 2

	auto &items = App::webPageItems();
	ids.reserve(_webPagesPending.size());
	int32 t = unixtime(), m = INT_MAX;
	for (auto i = _webPagesPending.begin(); i != _webPagesPending.cend(); ++i) {
		if (i.value() > 0) continue;
		if (i.key()->pendingTill <= t) {
			auto j = items.constFind(i.key());
			if (j != items.cend() && !j.value().isEmpty()) {
				for_const (auto item, j.value()) {
					if (item->id > 0) {
						if (item->channelId() == NoChannel) {
							ids.push_back(MTP_int(item->id));
							i.value() = -1;
						} else {
							auto channel = item->history()->peer->asChannel();
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

	mtpRequestId req = ids.isEmpty() ? 0 : MTP::send(MTPmessages_GetMessages(MTP_vector<MTPint>(ids)), rpcDone(&ApiWrap::gotWebPages, (ChannelData*)nullptr), RPCFailHandlerPtr(), 0, 5);
	using RequestIds = QVector<mtpRequestId>;
	RequestIds reqsByIndex(idsByChannel.size(), 0);
	for (auto i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i.value().first] = MTP::send(MTPchannels_GetMessages(i.key()->inputChannel, MTP_vector<MTPint>(i.value().second)), rpcDone(&ApiWrap::gotWebPages, i.key()), RPCFailHandlerPtr(), 0, 5);
	}
	if (req || !reqsByIndex.isEmpty()) {
		for (auto &requestId : _webPagesPending) {
			if (requestId > 0) continue;
			if (requestId < 0) {
				if (requestId == -1) {
					requestId = req;
				} else {
					requestId = reqsByIndex[-requestId - 2];
				}
			}
		}
	}

	if (m < INT_MAX) _webPagesTimer.start(m * 1000);
}

void ApiWrap::delayedRequestParticipantsCount() {
	if (App::main() && App::main()->peer() && App::main()->peer()->isChannel()) {
		requestFullPeer(App::main()->peer());
	}
}

void ApiWrap::gotWebPages(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId req) {
	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		auto &d = msgs.c_messages_messages();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = msgs.c_messages_messagesSlice();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d = msgs.c_messages_channelMessages();
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotWebPages)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;
	}

	if (!v) return;
	QMap<uint64, int32> msgsIds; // copied from feedMsgs
	for (int32 i = 0, l = v->size(); i < l; ++i) {
		const auto &msg(v->at(i));
		switch (msg.type()) {
		case mtpc_message: msgsIds.insert((uint64(uint32(msg.c_message().vid.v)) << 32) | uint64(i), i); break;
		case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i); break;
		case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i); break;
		}
	}

	for_const (auto msgId, msgsIds) {
		if (auto item = App::histories().addNewMessage(v->at(msgId), NewMessageExisting)) {
			item->setPendingInitDimensions();
		}
	}

	auto &items = App::webPageItems();
	for (auto i = _webPagesPending.begin(); i != _webPagesPending.cend();) {
		if (i.value() == req) {
			if (i.key()->pendingTill > 0) {
				i.key()->pendingTill = -1;
				auto j = items.constFind(i.key());
				if (j != items.cend()) {
					for_const (auto item, j.value()) {
						item->setPendingInitDimensions();
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
}

void ApiWrap::stickerSetDisenableDone(const MTPmessages_StickerSetInstallResult &result, mtpRequestId req) {
	_stickerSetDisenableRequests.remove(req);
	if (_stickerSetDisenableRequests.isEmpty()) {
		stickersSaveOrder();
	}
}

bool ApiWrap::stickerSetDisenableFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	_stickerSetDisenableRequests.remove(req);
	if (_stickerSetDisenableRequests.isEmpty()) {
		stickersSaveOrder();
	}
	return true;
}

void ApiWrap::stickersSaveOrder() {
	if (_stickersOrder.size() > 1) {
		QVector<MTPlong> mtpOrder;
		mtpOrder.reserve(_stickersOrder.size());
		for_const (auto setId, _stickersOrder) {
			mtpOrder.push_back(MTP_long(setId));
		}

		MTPmessages_ReorderStickerSets::Flags flags = 0;
		_stickersReorderRequestId = MTP::send(MTPmessages_ReorderStickerSets(MTP_flags(flags), MTP_vector<MTPlong>(mtpOrder)), rpcDone(&ApiWrap::stickersReorderDone), rpcFail(&ApiWrap::stickersReorderFail));
	} else {
		stickersReorderDone(MTP_boolTrue());
	}
}

void ApiWrap::stickersReorderDone(const MTPBool &result) {
	_stickersReorderRequestId = 0;
}

bool ApiWrap::stickersReorderFail(const RPCError &result) {
	if (MTP::isDefaultHandledError(result)) return false;
	_stickersReorderRequestId = 0;
	Global::SetLastStickersUpdate(0);
	App::main()->updateStickers();
	return true;
}

void ApiWrap::stickersClearRecentDone(const MTPBool &result) {
	_stickersClearRecentRequestId = 0;
}

bool ApiWrap::stickersClearRecentFail(const RPCError &result) {
	if (MTP::isDefaultHandledError(result)) return false;
	_stickersClearRecentRequestId = 0;
	return true;
}
