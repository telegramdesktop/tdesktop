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
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "messenger.h"
#include "mainwidget.h"
#include "history/history_widget.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "chat_helpers/message_field.h"

namespace {

constexpr auto kReloadChannelMembersTimeout = 1000; // 1 second wait before reload members in channel after adding
constexpr auto kSaveCloudDraftTimeout = 1000; // save draft to the cloud with 1 sec extra delay
constexpr auto kSaveDraftBeforeQuitTimeout = 1500; // give the app 1.5 secs to save drafts to cloud when quitting
constexpr auto kSmallDelayMs = 5;

} // namespace

ApiWrap::ApiWrap(gsl::not_null<AuthSession*> session)
: _session(session)
, _messageDataResolveDelayed([this] { resolveMessageDatas(); })
, _webPagesTimer([this] { resolveWebPages(); })
, _draftsSaveTimer([this] { saveDraftsToCloud(); }) {
}

void ApiWrap::start() {
	Window::Theme::Background()->start();
	requestAppChangelogs();
}

void ApiWrap::requestAppChangelogs() {
	auto oldAppVersion = Local::oldMapVersion();
	if (oldAppVersion > 0 && oldAppVersion < AppVersion) {
		_changelogSubscription = subscribe(_session->data().moreChatsLoaded(), [this, oldAppVersion] {
			auto oldVersionString = qsl("%1.%2.%3").arg(oldAppVersion / 1000000).arg((oldAppVersion % 1000000) / 1000).arg(oldAppVersion % 1000);
			request(MTPhelp_GetAppChangelog(MTP_string(oldVersionString))).done([this, oldAppVersion](const MTPUpdates &result) {
				applyUpdates(result);

				auto resultEmpty = true;
				switch (result.type()) {
				case mtpc_updateShortMessage:
				case mtpc_updateShortChatMessage:
				case mtpc_updateShort: resultEmpty = false; break;
				case mtpc_updatesCombined: resultEmpty = result.c_updatesCombined().vupdates.v.isEmpty(); break;
				case mtpc_updates: resultEmpty = result.c_updates().vupdates.v.isEmpty(); break;
				case mtpc_updatesTooLong:
				case mtpc_updateShortSentMessage: LOG(("API Error: Bad updates type in app changelog.")); break;
				}
				if (resultEmpty) {
					addLocalChangelogs(oldAppVersion);
				}
			}).send();
			unsubscribe(base::take(_changelogSubscription));
		});
	}
}

void ApiWrap::addLocalChangelogs(int oldAppVersion) {
	auto addedSome = false;
	auto addLocalChangelog = [this, &addedSome](const QString &text) {
		auto textWithEntities = TextWithEntities { text };
		TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
		App::wnd()->serviceNotification(textWithEntities, MTP_messageMediaEmpty(), unixtime());
		addedSome = true;
	};
	if (cAlphaVersion() || cBetaVersion()) {
		auto addLocalAlphaChangelog = [this, oldAppVersion, addLocalChangelog](int changeVersion, const char *changes) {
			if (oldAppVersion < changeVersion) {
				auto changeVersionString = QString::number(changeVersion / 1000000) + '.' + QString::number((changeVersion % 1000000) / 1000) + ((changeVersion % 1000) ? ('.' + QString::number(changeVersion % 1000)) : QString());
				auto text = qsl("New in version %1:\n\n").arg(changeVersionString) + QString::fromUtf8(changes).trimmed();
				addLocalChangelog(text);
			}
		};
		addLocalAlphaChangelog(1001008, "\xE2\x80\x94 Toggle night mode in the main menu.\n");
		addLocalAlphaChangelog(1001010, "\xE2\x80\x94 Filter added to channel and supergroup event log.\n\xE2\x80\x94 Search by username in privacy exceptions editor fixed.\n\xE2\x80\x94 Adding admins in channels fixed.");
		addLocalAlphaChangelog(1001011, "\xE2\x80\x94 Send **bold** and __italic__ text in your messages (in addition to already supported `monospace` and ```multiline monospace```).\n\xE2\x80\x94 Search in channel and supergroup admin event log.\n\xE2\x80\x94 Ban members from right click menu in supergroup admin event log.");
		addLocalAlphaChangelog(1001012, "\xE2\x80\x94 Click on forwarded messages bar to change the recipient chat in case you chose a wrong one first.\n\xE2\x80\x94 Quickly share posts from channels and media messages from bots.\n\xE2\x80\x94 Search in large supergroup members by name.\n\xE2\x80\x94 Search in channel members by name if you're a channel admin.\n\xE2\x80\x94 Copy links to messages in public supergroups.");
		addLocalAlphaChangelog(1001014, "\xE2\x80\x94 Bug fixes and other minor improvements.");
	}
	if (!addedSome) {
		auto text = lng_new_version_wrap(lt_version, str_const_toString(AppVersionStr), lt_changes, lang(lng_new_version_minor), lt_link, qsl("https://desktop.telegram.org/changelog")).trimmed();
		addLocalChangelog(text);
	}
}

void ApiWrap::applyUpdates(const MTPUpdates &updates, uint64 sentMessageRandomId) {
	App::main()->feedUpdates(updates, sentMessageRandomId);
}

void ApiWrap::requestMessageData(ChannelData *channel, MsgId msgId, RequestMessageDataCallback callback) {
	auto &req = (channel ? _channelMessageDataRequests[channel][msgId] : _messageDataRequests[msgId]);
	if (callback) {
		req.callbacks.append(callback);
	}
	if (!req.requestId) _messageDataResolveDelayed.call();
}

QVector<MTPint> ApiWrap::collectMessageIds(const MessageDataRequests &requests) {
	auto result = QVector<MTPint>();
	result.reserve(requests.size());
	for (auto i = requests.cbegin(), e = requests.cend(); i != e; ++i) {
		if (i.value().requestId > 0) continue;
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

	auto ids = collectMessageIds(_messageDataRequests);
	if (!ids.isEmpty()) {
		auto requestId = request(MTPmessages_GetMessages(MTP_vector<MTPint>(ids))).done([this](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotMessageDatas(nullptr, result, requestId);
		}).after(kSmallDelayMs).send();
		for (auto &request : _messageDataRequests) {
			if (request.requestId > 0) continue;
			request.requestId = requestId;
		}
	}
	for (auto j = _channelMessageDataRequests.begin(); j != _channelMessageDataRequests.cend();) {
		if (j->isEmpty()) {
			j = _channelMessageDataRequests.erase(j);
			continue;
		}
		auto ids = collectMessageIds(j.value());
		if (!ids.isEmpty()) {
			auto requestId = request(MTPchannels_GetMessages(j.key()->inputChannel, MTP_vector<MTPint>(ids))).done([this, channel = j.key()](const MTPmessages_Messages &result, mtpRequestId requestId) {
				gotMessageDatas(channel, result, requestId);
			}).after(kSmallDelayMs).send();

			for (auto &request : *j) {
				if (request.requestId > 0) continue;
				request.requestId = requestId;
			}
		}
		++j;
	}
}

void ApiWrap::gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId requestId) {
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
	auto requests = messageDataRequests(channel, true);
	if (requests) {
		for (auto i = requests->begin(); i != requests->cend();) {
			if (i.value().requestId == requestId) {
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

	auto sendRequest = [this, peer] {
		auto failHandler = [this, peer](const RPCError &error) {
			_fullPeerRequests.remove(peer);
		};
		if (auto user = peer->asUser()) {
			return request(MTPusers_GetFullUser(user->inputUser)).done([this, user](const MTPUserFull &result, mtpRequestId requestId) {
				gotUserFull(user, result, requestId);
			}).fail(failHandler).send();
		} else if (auto chat = peer->asChat()) {
			return request(MTPmessages_GetFullChat(chat->inputChat)).done([this, peer](const MTPmessages_ChatFull &result, mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
			}).fail(failHandler).send();
		} else if (auto channel = peer->asChannel()) {
			return request(MTPchannels_GetFullChannel(channel->inputChannel)).done([this, peer](const MTPmessages_ChatFull &result, mtpRequestId requestId) {
				gotChatFull(peer, result, requestId);
			}).fail(failHandler).send();
		}
		return 0;
	};
	if (auto requestId = sendRequest()) {
		_fullPeerRequests.insert(peer, requestId);
	}
}

void ApiWrap::processFullPeer(PeerData *peer, const MTPmessages_ChatFull &result) {
	gotChatFull(peer, result, mtpRequestId(0));
}

void ApiWrap::processFullPeer(UserData *user, const MTPUserFull &result) {
	gotUserFull(user, result, mtpRequestId(0));
}

void ApiWrap::gotChatFull(PeerData *peer, const MTPmessages_ChatFull &result, mtpRequestId req) {
	auto &d = result.c_messages_chatFull();
	auto &vc = d.vchats.v;
	auto badVersion = false;
	if (peer->isChat()) {
		badVersion = (!vc.isEmpty() && vc[0].type() == mtpc_chat && vc[0].c_chat().vversion.v < peer->asChat()->version);
	} else if (peer->isChannel()) {
		badVersion = (!vc.isEmpty() && vc[0].type() == mtpc_channel && vc[0].c_channel().vversion.v < peer->asChannel()->version);
	}

	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);

	if (auto chat = peer->asChat()) {
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
					fullPeerUpdated().notify(user);
				}
			} break;
			}
		}
		if (auto photo = App::feedPhoto(f.vchat_photo)) {
			chat->photoId = photo->id;
			photo->peer = chat;
		} else {
			chat->photoId = 0;
		}
		chat->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		chat->fullUpdated();

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	} else if (auto channel = peer->asChannel()) {
		if (d.vfull_chat.type() != mtpc_channelFull) {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_channelFull();

		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();

		channel->flagsFull = f.vflags.v;
		if (auto photo = App::feedPhoto(f.vchat_photo)) {
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
			auto cfrom = App::chat(peerFromChat(f.vmigrated_from_chat_id));
			bool updatedTo = (cfrom->migrateToPtr != channel), updatedFrom = (channel->mgInfo->migrateFromPtr != cfrom);
			if (updatedTo) {
				cfrom->migrateToPtr = channel;
			}
			if (updatedFrom) {
				channel->mgInfo->migrateFromPtr = cfrom;
				if (auto h = App::historyLoaded(cfrom->id)) {
					if (auto hto = App::historyLoaded(channel->id)) {
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
					fullPeerUpdated().notify(user);
				}
			} break;
			}
		}
		channel->setAbout(qs(f.vabout));
		channel->setMembersCount(f.has_participants_count() ? f.vparticipants_count.v : 0);
		channel->setAdminsCount(f.has_admins_count() ? f.vadmins_count.v : 0);
		channel->setRestrictedCount(f.has_banned_count() ? f.vbanned_count.v : 0);
		channel->setKickedCount(f.has_kicked_count() ? f.vkicked_count.v : 0);
		channel->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		if (auto h = App::historyLoaded(channel->id)) {
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

		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()) Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelRightsChanged);

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	}

	if (req) {
		auto i = _fullPeerRequests.find(peer);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	if (badVersion) {
		if (auto chat = peer->asChat()) {
			chat->version = vc[0].c_chat().vversion.v;
		} else if (auto channel = peer->asChannel()) {
			channel->version = vc[0].c_channel().vversion.v;
		}
		requestPeer(peer);
	}
	App::clearPeerUpdated(peer);
	fullPeerUpdated().notify(peer);
}

void ApiWrap::gotUserFull(UserData *user, const MTPUserFull &result, mtpRequestId req) {
	auto &d = result.c_userFull();
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	if (d.has_profile_photo()) {
		App::feedPhoto(d.vprofile_photo);
	}
	App::feedUserLink(MTP_int(peerToUser(user->id)), d.vlink.c_contacts_link().vmy_link, d.vlink.c_contacts_link().vforeign_link);
	if (App::main()) {
		notifySettingReceived(MTP_inputNotifyPeer(user->input), d.vnotify_settings);
	}

	if (d.has_bot_info()) {
		user->setBotInfo(d.vbot_info);
	} else {
		user->setBotInfoVersion(-1);
	}
	user->setBlockStatus(d.is_blocked() ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
	user->setCallsStatus(d.is_phone_calls_private() ? UserData::CallsStatus::Private : d.is_phone_calls_available() ? UserData::CallsStatus::Enabled : UserData::CallsStatus::Disabled);
	user->setAbout(d.has_about() ? qs(d.vabout) : QString());
	user->setCommonChatsCount(d.vcommon_chats_count.v);
	user->fullUpdated();

	if (req) {
		auto i = _fullPeerRequests.find(user);
		if (i != _fullPeerRequests.cend() && i.value() == req) {
			_fullPeerRequests.erase(i);
		}
	}
	App::clearPeerUpdated(user);
	fullPeerUpdated().notify(user);
}

void ApiWrap::requestPeer(PeerData *peer) {
	if (!peer || _fullPeerRequests.contains(peer) || _peerRequests.contains(peer)) return;

	auto sendRequest = [this, peer] {
		auto failHandler = [this, peer](const RPCError &error) {
			_peerRequests.remove(peer);
		};
		auto chatHandler = [this, peer](const MTPmessages_Chats &result) {
			_peerRequests.remove(peer);

			if (auto chats = Api::getChatsFromMessagesChats(result)) {
				auto &v = chats->v;
				bool badVersion = false;
				if (auto chat = peer->asChat()) {
					badVersion = (!v.isEmpty() && v[0].type() == mtpc_chat && v[0].c_chat().vversion.v < chat->version);
				} else if (auto channel = peer->asChannel()) {
					badVersion = (!v.isEmpty() && v[0].type() == mtpc_channel && v[0].c_channel().vversion.v < channel->version);
				}
				auto chat = App::feedChats(*chats);
				if (chat == peer) {
					if (badVersion) {
						if (auto chat = peer->asChat()) {
							chat->version = v[0].c_chat().vversion.v;
						} else if (auto channel = peer->asChannel()) {
							channel->version = v[0].c_channel().vversion.v;
						}
						requestPeer(peer);
					}
				}
			}
		};
		if (auto user = peer->asUser()) {
			return request(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, user->inputUser))).done([this, user](const MTPVector<MTPUser> &result) {
				_peerRequests.remove(user);
				App::feedUsers(result);
			}).fail(failHandler).send();
		} else if (auto chat = peer->asChat()) {
			return request(MTPmessages_GetChats(MTP_vector<MTPint>(1, chat->inputChat))).done(chatHandler).fail(failHandler).send();
		} else if (auto channel = peer->asChannel()) {
			return request(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(1, channel->inputChannel))).done(chatHandler).fail(failHandler).send();
		}
		return 0;
	};
	if (auto requestId = sendRequest()) {
		_peerRequests.insert(peer, requestId);
	}
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
	auto handleChats = [this](const MTPmessages_Chats &result) {
		if (auto chats = Api::getChatsFromMessagesChats(result)) {
			App::feedChats(*chats);
		}
	};
	if (!chats.isEmpty()) {
		request(MTPmessages_GetChats(MTP_vector<MTPint>(chats))).done(handleChats).send();
	}
	if (!channels.isEmpty()) {
		request(MTPchannels_GetChannels(MTP_vector<MTPInputChannel>(channels))).done(handleChats).send();
	}
	if (!users.isEmpty()) {
		request(MTPusers_GetUsers(MTP_vector<MTPInputUser>(users))).done([this](const MTPVector<MTPUser> &result) {
			App::feedUsers(result);
		}).send();
	}
}

void ApiWrap::requestLastParticipants(ChannelData *channel, bool fromStart) {
	if (!channel || !channel->isMegagroup()) {
		return;
	}

	auto needAdmins = channel->canViewAdmins();
	auto adminsOutdated = (channel->mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsAdminsOutdated) != 0;
	if ((needAdmins && adminsOutdated) || channel->lastParticipantsCountOutdated()) {
		fromStart = true;
	}
	auto i = _participantsRequests.find(channel);
	if (i != _participantsRequests.cend()) {
		if (fromStart && i.value() < 0) { // was not loading from start
			_participantsRequests.erase(i);
		} else {
			return;
		}
	}

	auto requestId = request(MTPchannels_GetParticipants(channel->inputChannel, MTP_channelParticipantsRecent(), MTP_int(fromStart ? 0 : channel->mgInfo->lastParticipants.size()), MTP_int(Global::ChatSizeMax()))).done([this, channel](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
		lastParticipantsDone(channel, result, requestId);
	}).fail([this, channel](const RPCError &error, mtpRequestId requestId) {
		if (_participantsRequests.value(channel) == requestId || _participantsRequests.value(channel) == -requestId) {
			_participantsRequests.remove(channel);
		}
	}).send();

	_participantsRequests.insert(channel, fromStart ? requestId : -requestId);
}

void ApiWrap::requestBots(ChannelData *channel) {
	if (!channel || !channel->isMegagroup() || _botsRequests.contains(channel)) {
		return;
	}

	auto requestId = request(MTPchannels_GetParticipants(channel->inputChannel, MTP_channelParticipantsBots(), MTP_int(0), MTP_int(Global::ChatSizeMax()))).done([this, channel](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
		lastParticipantsDone(channel, result, requestId);
	}).fail([this, channel](const RPCError &error, mtpRequestId requestId) {
		if (_botsRequests.value(channel) == requestId) {
			_botsRequests.remove(channel);
		}
	}).send();

	_botsRequests.insert(channel, requestId);
}

void ApiWrap::lastParticipantsDone(ChannelData *peer, const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
	bool bots = (_botsRequests.value(peer) == requestId), fromStart = false;
	if (bots) {
		_botsRequests.remove(peer);
	} else {
		auto was = _participantsRequests.value(peer);
		if (was == requestId) {
			fromStart = true;
		} else if (was != -requestId) {
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
	auto added = false;
	auto needBotsInfos = false;
	auto botStatus = peer->mgInfo->botStatus;
	auto keyboardBotFound = !h || !h->lastKeyboardFrom;
	auto emptyAdminRights = MTP_channelAdminRights(MTP_flags(0));
	auto emptyRestrictedRights = MTP_channelBannedRights(MTP_flags(0), MTP_int(0));
	for_const (auto &participant, v) {
		auto userId = UserId(0);
		auto adminCanEdit = false;
		auto adminRights = emptyAdminRights;
		auto restrictedRights = emptyRestrictedRights;

		switch (participant.type()) {
		case mtpc_channelParticipant: userId = participant.c_channelParticipant().vuser_id.v; break;
		case mtpc_channelParticipantSelf: userId = participant.c_channelParticipantSelf().vuser_id.v; break;
		case mtpc_channelParticipantAdmin:
			userId = participant.c_channelParticipantAdmin().vuser_id.v;
			adminCanEdit = participant.c_channelParticipantAdmin().is_can_edit();
			adminRights = participant.c_channelParticipantAdmin().vadmin_rights;
		break;
		case mtpc_channelParticipantBanned:
			userId = participant.c_channelParticipantBanned().vuser_id.v;
			restrictedRights = participant.c_channelParticipantBanned().vbanned_rights;
		break;
		case mtpc_channelParticipantCreator: userId = participant.c_channelParticipantCreator().vuser_id.v; break;
		}
		if (!userId) {
			continue;
		}

		auto u = App::user(userId);
		if (participant.type() == mtpc_channelParticipantCreator) {
			peer->mgInfo->creator = u;
		}
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
				if (adminRights.c_channelAdminRights().vflags.v) {
					peer->mgInfo->lastAdmins.insert(u, MegagroupInfo::Admin { adminRights, adminCanEdit });
				} else if (restrictedRights.c_channelBannedRights().vflags.v != 0) {
					peer->mgInfo->lastRestricted.insert(u, MegagroupInfo::Restricted { restrictedRights });
				}
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
	if (App::main()) fullPeerUpdated().notify(peer);
}

void ApiWrap::requestSelfParticipant(ChannelData *channel) {
	if (_selfParticipantRequests.contains(channel)) {
		return;
	}

	auto requestId = request(MTPchannels_GetParticipant(channel->inputChannel, MTP_inputUserSelf())).done([this, channel](const MTPchannels_ChannelParticipant &result) {
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
			channel->inviter = _session->userId();
			channel->inviteDate = date(MTP_int(channel->date));
			if (channel->mgInfo) {
				channel->mgInfo->creator = App::self();
			}
		} break;
		case mtpc_channelParticipantAdmin: {
			auto &d = p.vparticipant.c_channelParticipantAdmin();
			channel->inviter = d.vinviter_id.v;
			channel->inviteDate = date(d.vdate);
		} break;
		}

		if (App::main()) App::main()->onSelfParticipantUpdated(channel);
	}).fail([this, channel](const RPCError &error) {
		_selfParticipantRequests.remove(channel);
		if (error.type() == qstr("USER_NOT_PARTICIPANT")) {
			channel->inviter = -1;
		}
	}).after(kSmallDelayMs).send();

	_selfParticipantRequests.insert(channel, requestId);
}

void ApiWrap::kickParticipant(PeerData *peer, UserData *user, const MTPChannelBannedRights &currentRights) {
	auto kick = KickRequest(peer, user);
	if (_kickRequests.contains(kick)) return;

	if (auto channel = peer->asChannel()) {
		auto rights = ChannelData::KickedRestrictedRights();
		auto requestId = request(MTPchannels_EditBanned(channel->inputChannel, user->inputUser, rights)).done([this, channel, user, currentRights, rights](const MTPUpdates &result) {
			applyUpdates(result);

			_kickRequests.remove(KickRequest(channel, user));
			channel->applyEditBanned(user, currentRights, rights);
		}).fail([this, kick](const RPCError &error) {
			_kickRequests.remove(kick);
		}).send();

		_kickRequests.insert(kick, requestId);
	}
}

void ApiWrap::unblockParticipant(PeerData *peer, UserData *user) {
	auto kick = KickRequest(peer, user);
	if (_kickRequests.contains(kick)) return;

	if (auto channel = peer->asChannel()) {
		auto requestId = request(MTPchannels_EditBanned(channel->inputChannel, user->inputUser, MTP_channelBannedRights(MTP_flags(0), MTP_int(0)))).done([this, peer, user](const MTPUpdates &result) {
			applyUpdates(result);

			_kickRequests.remove(KickRequest(peer, user));
			if (auto channel = peer->asMegagroup()) {
				if (channel->kickedCount() > 0) {
					channel->setKickedCount(channel->kickedCount() - 1);
				} else {
					channel->updateFullForced();
				}
			}
		}).fail([this, kick](const RPCError &error) {
			_kickRequests.remove(kick);
		}).send();

		_kickRequests.insert(kick, requestId);
	}
}

void ApiWrap::scheduleStickerSetRequest(uint64 setId, uint64 access) {
	if (!_stickerSetRequests.contains(setId)) {
		_stickerSetRequests.insert(setId, qMakePair(access, 0));
	}
}

void ApiWrap::requestStickerSets() {
	for (auto i = _stickerSetRequests.begin(), j = i, e = _stickerSetRequests.end(); i != e; i = j) {
		++j;
		if (i.value().second) continue;

		auto waitMs = (j == e) ? 0 : kSmallDelayMs;
		i.value().second = request(MTPmessages_GetStickerSet(MTP_inputStickerSetID(MTP_long(i.key()), MTP_long(i.value().first)))).done([this, setId = i.key()](const MTPmessages_StickerSet &result) {
			gotStickerSet(setId, result);
		}).fail([this, setId = i.key()](const RPCError &error) {
			_stickerSetRequests.remove(setId);
		}).after(waitMs).send();
	}
}

void ApiWrap::saveStickerSets(const Stickers::Order &localOrder, const Stickers::Order &localRemoved) {
	for (auto requestId : base::take(_stickerSetDisenableRequests)) {
		request(requestId).cancel();
	}
	request(base::take(_stickersReorderRequestId)).cancel();
	request(base::take(_stickersClearRecentRequestId)).cancel();

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

			_stickersClearRecentRequestId = request(MTPmessages_ClearRecentStickers(MTP_flags(0))).done([this](const MTPBool &result) {
				_stickersClearRecentRequestId = 0;
			}).fail([this](const RPCError &error) {
				_stickersClearRecentRequestId = 0;
			}).send();
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

				auto requestId = request(MTPmessages_UninstallStickerSet(setId)).done([this](const MTPBool &result, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).fail([this](const RPCError &error, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).after(kSmallDelayMs).send();

				_stickerSetDisenableRequests.insert(requestId);

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

				auto requestId = request(MTPmessages_InstallStickerSet(mtpSetId, MTP_boolFalse())).done([this](const MTPmessages_StickerSetInstallResult &result, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).fail([this](const RPCError &error, mtpRequestId requestId) {
					stickerSetDisenabled(requestId);
				}).after(kSmallDelayMs).send();

				_stickerSetDisenableRequests.insert(requestId);

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
		requestSendDelayed();
	}
}

void ApiWrap::stickerSetDisenabled(mtpRequestId requestId) {
	_stickerSetDisenableRequests.remove(requestId);
	if (_stickerSetDisenableRequests.isEmpty()) {
		stickersSaveOrder();
	}
};

void ApiWrap::joinChannel(ChannelData *channel) {
	if (channel->amIn()) {
		Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = request(MTPchannels_JoinChannel(channel->inputChannel)).done([this, channel](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([this, channel](const RPCError &error) {
			if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
				Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
			}
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::leaveChannel(ChannelData *channel) {
	if (!channel->amIn()) {
		Notify::peerUpdatedDelayed(channel, Notify::PeerUpdate::Flag::ChannelAmIn);
	} else if (!_channelAmInRequests.contains(channel)) {
		auto requestId = request(MTPchannels_LeaveChannel(channel->inputChannel)).done([this, channel](const MTPUpdates &result) {
			_channelAmInRequests.remove(channel);
			applyUpdates(result);
		}).fail([this, channel](const RPCError &error) {
			_channelAmInRequests.remove(channel);
		}).send();

		_channelAmInRequests.insert(channel, requestId);
	}
}

void ApiWrap::blockUser(UserData *user) {
	if (user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (!_blockRequests.contains(user)) {
		auto requestId = request(MTPcontacts_Block(user->inputUser)).done([this, user](const MTPBool &result) {
			_blockRequests.remove(user);
			user->setBlockStatus(UserData::BlockStatus::Blocked);
			emit App::main()->peerUpdated(user);
		}).fail([this, user](const RPCError &error) {
			_blockRequests.remove(user);
		}).send();

		_blockRequests.insert(user, requestId);
	}
}

void ApiWrap::unblockUser(UserData *user) {
	if (!user->isBlocked()) {
		Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsBlocked);
	} else if (!_blockRequests.contains(user)) {
		auto requestId = request(MTPcontacts_Unblock(user->inputUser)).done([this, user](const MTPBool &result) {
			_blockRequests.remove(user);
			user->setBlockStatus(UserData::BlockStatus::NotBlocked);
			emit App::main()->peerUpdated(user);
		}).fail([this, user](const RPCError &error) {
			_blockRequests.remove(user);
		}).send();

		_blockRequests.insert(user, requestId);
	}
}

void ApiWrap::exportInviteLink(PeerData *peer) {
	if (_exportInviteRequests.contains(peer)) {
		return;
	}

	auto sendRequest = [this, peer] {
		auto exportFail = [this, peer](const RPCError &error) {
			_exportInviteRequests.remove(peer);
		};
		if (auto chat = peer->asChat()) {
			return request(MTPmessages_ExportChatInvite(chat->inputChat)).done([this, chat](const MTPExportedChatInvite &result) {
				_exportInviteRequests.remove(chat);
				chat->setInviteLink((result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString());
			}).fail(exportFail).send();
		} else if (auto channel = peer->asChannel()) {
			return request(MTPchannels_ExportInvite(channel->inputChannel)).done([this, channel](const MTPExportedChatInvite &result) {
				_exportInviteRequests.remove(channel);
				channel->setInviteLink((result.type() == mtpc_chatInviteExported) ? qs(result.c_chatInviteExported().vlink) : QString());
			}).fail(exportFail).send();
		}
		return 0;
	};
	if (auto requestId = sendRequest()) {
		_exportInviteRequests.insert(peer, requestId);
	}
}

void ApiWrap::requestNotifySetting(PeerData *peer) {
	if (_notifySettingRequests.contains(peer)) return;

	auto notifyPeer = MTP_inputNotifyPeer(peer->input);
	auto requestId = request(MTPaccount_GetNotifySettings(notifyPeer)).done([this, notifyPeer, peer](const MTPPeerNotifySettings &result) {
		notifySettingReceived(notifyPeer, result);
		_notifySettingRequests.remove(peer);
	}).fail([this, notifyPeer, peer](const RPCError &error) {
		notifySettingReceived(notifyPeer, MTP_peerNotifySettingsEmpty());
		_notifySettingRequests.remove(peer);
	}).send();

	_notifySettingRequests.insert(peer, requestId);
}

void ApiWrap::saveDraftToCloudDelayed(History *history) {
	_draftsSaveRequestIds.insert(history, 0);
	if (!_draftsSaveTimer.isActive()) {
		_draftsSaveTimer.callOnce(kSaveCloudDraftTimeout);
	}
}

void ApiWrap::savePrivacy(const MTPInputPrivacyKey &key, QVector<MTPInputPrivacyRule> &&rules) {
	auto keyTypeId = key.type();
	auto it = _privacySaveRequests.find(keyTypeId);
	if (it != _privacySaveRequests.cend()) {
		request(it.value()).cancel();
		_privacySaveRequests.erase(it);
	}

	auto requestId = request(MTPaccount_SetPrivacy(key, MTP_vector<MTPInputPrivacyRule>(std::move(rules)))).done([this, keyTypeId](const MTPaccount_PrivacyRules &result) {
		Expects(result.type() == mtpc_account_privacyRules);
		auto &rules = result.c_account_privacyRules();
		App::feedUsers(rules.vusers);
		_privacySaveRequests.remove(keyTypeId);
		handlePrivacyChange(keyTypeId, rules.vrules);
	}).fail([this, keyTypeId](const RPCError &error) {
		_privacySaveRequests.remove(keyTypeId);
	}).send();

	_privacySaveRequests.insert(keyTypeId, requestId);
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
			request(_contactsStatusesRequestId).cancel();
		}
		_contactsStatusesRequestId = request(MTPcontacts_GetStatuses()).done([this](const MTPVector<MTPContactStatus> &result) {
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
		}).fail([this](const RPCError &error) {
			_contactsStatusesRequestId = 0;
		}).send();
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

void ApiWrap::saveDraftsToCloud() {
	for (auto i = _draftsSaveRequestIds.begin(), e = _draftsSaveRequestIds.end(); i != e; ++i) {
		if (i.value()) continue; // sent already

		auto history = i.key();
		auto cloudDraft = history->cloudDraft();
		auto localDraft = history->localDraft();
		if (cloudDraft && cloudDraft->saveRequestId) {
			request(base::take(cloudDraft->saveRequestId)).cancel();
		}
		cloudDraft = history->createCloudDraft(localDraft);

		auto flags = MTPmessages_SaveDraft::Flags(0);
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
		auto entities = TextUtilities::EntitiesToMTP(ConvertTextTagsToEntities(textWithTags.tags), TextUtilities::ConvertOption::SkipLocal);

		cloudDraft->saveRequestId = request(MTPmessages_SaveDraft(MTP_flags(flags), MTP_int(cloudDraft->msgId), history->peer->input, MTP_string(textWithTags.text), entities)).done([this, history](const MTPBool &result, mtpRequestId requestId) {
			if (auto cloudDraft = history->cloudDraft()) {
				if (cloudDraft->saveRequestId == requestId) {
					cloudDraft->saveRequestId = 0;
					history->draftSavedToCloud();
				}
			}
			auto i = _draftsSaveRequestIds.find(history);
			if (i != _draftsSaveRequestIds.cend() && i.value() == requestId) {
				_draftsSaveRequestIds.remove(history);
				checkQuitPreventFinished();
			}
		}).fail([this, history](const RPCError &error, mtpRequestId requestId) {
			if (auto cloudDraft = history->cloudDraft()) {
				if (cloudDraft->saveRequestId == requestId) {
					history->clearCloudDraft();
				}
			}
			auto i = _draftsSaveRequestIds.find(history);
			if (i != _draftsSaveRequestIds.cend() && i.value() == requestId) {
				_draftsSaveRequestIds.remove(history);
				checkQuitPreventFinished();
			}
		}).send();

		i.value() = cloudDraft->saveRequestId;
	}
}

bool ApiWrap::isQuitPrevent() {
	if (_draftsSaveRequestIds.isEmpty()) {
		return false;
	}
	LOG(("ApiWrap prevents quit, saving drafts..."));
	saveDraftsToCloud();
	return true;
}

void ApiWrap::checkQuitPreventFinished() {
	if (_draftsSaveRequestIds.isEmpty()) {
		if (App::quitting()) {
			LOG(("ApiWrap doesn't prevent quit any more."));
		}
		Messenger::Instance().quitPreventFinished();
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
	_session->notifications().checkDelayed();
	return requestedPeer;
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

void ApiWrap::requestWebPageDelayed(WebPageData *page) {
	if (page->pendingTill <= 0) return;
	_webPagesPending.insert(page, 0);
	auto left = (page->pendingTill - unixtime()) * 1000;
	if (!_webPagesTimer.isActive() || left <= _webPagesTimer.remainingTime()) {
		_webPagesTimer.callOnce((left < 0 ? 0 : left) + 1);
	}
}

void ApiWrap::clearWebPageRequest(WebPageData *page) {
	_webPagesPending.remove(page);
	if (_webPagesPending.isEmpty() && _webPagesTimer.isActive()) {
		_webPagesTimer.cancel();
	}
}

void ApiWrap::clearWebPageRequests() {
	_webPagesPending.clear();
	_webPagesTimer.cancel();
}

void ApiWrap::resolveWebPages() {
	auto ids = QVector<MTPint>(); // temp_req_id = -1
	using IndexAndMessageIds = QPair<int32, QVector<MTPint>>;
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
							auto channelMap = idsByChannel.find(channel);
							if (channelMap == idsByChannel.cend()) {
								channelMap = idsByChannel.insert(channel, IndexAndMessageIds(idsByChannel.size(), QVector<MTPint>(1, MTP_int(item->id))));
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

	auto requestId = mtpRequestId(0);
	if (!ids.isEmpty()) {
		requestId = request(MTPmessages_GetMessages(MTP_vector<MTPint>(ids))).done([this](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotWebPages(nullptr, result, requestId);
		}).after(kSmallDelayMs).send();
	}
	QVector<mtpRequestId> reqsByIndex(idsByChannel.size(), 0);
	for (auto i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i.value().first] = request(MTPchannels_GetMessages(i.key()->inputChannel, MTP_vector<MTPint>(i.value().second))).done([this, channel = i.key()](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotWebPages(channel, result, requestId);
		}).after(kSmallDelayMs).send();
	}
	if (requestId || !reqsByIndex.isEmpty()) {
		for (auto &pendingRequestId : _webPagesPending) {
			if (pendingRequestId > 0) continue;
			if (pendingRequestId < 0) {
				if (pendingRequestId == -1) {
					pendingRequestId = requestId;
				} else {
					pendingRequestId = reqsByIndex[-pendingRequestId - 2];
				}
			}
		}
	}

	if (m < INT_MAX) {
		_webPagesTimer.callOnce(m * 1000);
	}
}

void ApiWrap::requestParticipantsCountDelayed(ChannelData *channel) {
	_participantsCountRequestTimer.call(kReloadChannelMembersTimeout, [this, channel] {
		channel->updateFullForced();
	});
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

void ApiWrap::stickersSaveOrder() {
	if (_stickersOrder.size() > 1) {
		QVector<MTPlong> mtpOrder;
		mtpOrder.reserve(_stickersOrder.size());
		for_const (auto setId, _stickersOrder) {
			mtpOrder.push_back(MTP_long(setId));
		}

		_stickersReorderRequestId = request(MTPmessages_ReorderStickerSets(MTP_flags(0), MTP_vector<MTPlong>(mtpOrder))).done([this](const MTPBool &result) {
			_stickersReorderRequestId = 0;
		}).fail([this](const RPCError &error) {
			_stickersReorderRequestId = 0;
			Global::SetLastStickersUpdate(0);
			App::main()->updateStickers();
		}).send();
	}
}

void ApiWrap::applyUpdatesNoPtsCheck(const MTPUpdates &updates) {
	switch (updates.type()) {
	case mtpc_updateShortMessage: {
		auto &d = updates.c_updateShortMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		App::histories().addNewMessage(MTP_message(MTP_flags(flags), d.vid, d.is_out() ? MTP_int(AuthSession::CurrentUserId()) : d.vuser_id, MTP_peerUser(d.is_out() ? d.vuser_id : MTP_int(AuthSession::CurrentUserId())), d.vfwd_from, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint(), MTPint(), MTPstring()), NewMessageUnread);
	} break;

	case mtpc_updateShortChatMessage: {
		auto &d = updates.c_updateShortChatMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		App::histories().addNewMessage(MTP_message(MTP_flags(flags), d.vid, d.vfrom_id, MTP_peerChat(d.vchat_id), d.vfwd_from, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint(), MTPint(), MTPstring()), NewMessageUnread);
	} break;

	case mtpc_updateShortSentMessage: {
		auto &d = updates.c_updateShortSentMessage();
		Q_UNUSED(d); // Sent message data was applied anyway.
	} break;

	default: Unexpected("Type in applyUpdatesNoPtsCheck()");
	}
}

void ApiWrap::applyUpdateNoPtsCheck(const MTPUpdate &update) {
	switch (update.type()) {
	case mtpc_updateNewMessage: {
		auto &d = update.c_updateNewMessage();
		auto needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			App::histories().addNewMessage(d.vmessage, NewMessageUnread);
		}
	} break;

	case mtpc_updateReadMessagesContents: {
		auto &d = update.c_updateReadMessagesContents();
		auto &v = d.vmessages.v;
		for (auto i = 0, l = v.size(); i < l; ++i) {
			if (auto item = App::histItemById(NoChannel, v.at(i).v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					Ui::repaintHistoryItem(item);

					if (item->out() && item->history()->peer->isUser()) {
						auto when = App::main()->requestingDifference() ? 0 : unixtime();
						item->history()->peer->asUser()->madeAction(when);
					}
				}
			}
		}
	} break;

	case mtpc_updateReadHistoryInbox: {
		auto &d = update.c_updateReadHistoryInbox();
		App::feedInboxRead(peerFromMTP(d.vpeer), d.vmax_id.v);
	} break;

	case mtpc_updateReadHistoryOutbox: {
		auto &d = update.c_updateReadHistoryOutbox();
		auto peerId = peerFromMTP(d.vpeer);
		auto when = App::main()->requestingDifference() ? 0 : unixtime();
		App::feedOutboxRead(peerId, d.vmax_id.v, when);
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();
		App::feedWereDeleted(NoChannel, d.vmessages.v);
	} break;

	case mtpc_updateNewChannelMessage: {
		auto &d = update.c_updateNewChannelMessage();
		auto needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			App::histories().addNewMessage(d.vmessage, NewMessageUnread);
		}
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		App::updateEditedMessage(d.vmessage);
	} break;

	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();
		App::updateEditedMessage(d.vmessage);
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();
		Q_UNUSED(d); // Web page was updated anyway.
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		App::feedWereDeleted(d.vchannel_id.v, d.vmessages.v);
	} break;

	default: Unexpected("Type in applyUpdateNoPtsCheck()");
	}
}

void ApiWrap::jumpToDate(gsl::not_null<PeerData*> peer, const QDate &date) {
	// API returns a message with date <= offset_date.
	// So we request a message with offset_date = desired_date - 1 and add_offset = -1.
	// This should give us the first message with date >= desired_date.
	auto offset_date = static_cast<int>(QDateTime(date).toTime_t()) - 1;
	auto add_offset = -1;
	auto limit = 1;
	request(MTPmessages_GetHistory(peer->input, MTP_int(0), MTP_int(offset_date), MTP_int(add_offset), MTP_int(limit), MTP_int(0), MTP_int(0))).done([peer](const MTPmessages_Messages &result) {
		auto getMessagesList = [&result, peer]() -> const QVector<MTPMessage>* {
			auto handleMessages = [](auto &messages) {
				App::feedUsers(messages.vusers);
				App::feedChats(messages.vchats);
				return &messages.vmessages.v;
			};
			switch (result.type()) {
			case mtpc_messages_messages: return handleMessages(result.c_messages_messages());
			case mtpc_messages_messagesSlice: return handleMessages(result.c_messages_messagesSlice());
			case mtpc_messages_channelMessages: {
				auto &messages = result.c_messages_channelMessages();
				if (peer && peer->isChannel()) {
					peer->asChannel()->ptsReceived(messages.vpts.v);
				} else {
					LOG(("API Error: received messages.channelMessages when no channel was passed! (MainWidget::showJumpToDate)"));
				}
				return handleMessages(messages);
			} break;
			}
			return nullptr;
		};

		if (auto list = getMessagesList()) {
			App::feedMsgs(*list, NewMessageExisting);
			for (auto &message : *list) {
				auto id = idFromMessage(message);
				Ui::showPeerHistory(peer, id);
				return;
			}
		}
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}).send();
}

ApiWrap::~ApiWrap() = default;
