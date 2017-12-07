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
#include "data/data_photo.h"
#include "data/data_web_page.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "observer_peer.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwindow.h"
#include "messenger.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "history/history_message.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "boxes/confirm_box.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"
#include "data/data_sparse_ids.h"
#include "data/data_search_controller.h"
#include "data/data_channel_admins.h"

namespace {

constexpr auto kReloadChannelMembersTimeout = 1000; // 1 second wait before reload members in channel after adding
constexpr auto kSaveCloudDraftTimeout = 1000; // save draft to the cloud with 1 sec extra delay
constexpr auto kSaveDraftBeforeQuitTimeout = 1500; // give the app 1.5 secs to save drafts to cloud when quitting
constexpr auto kSmallDelayMs = 5;
constexpr auto kUnreadMentionsPreloadIfLess = 5;
constexpr auto kUnreadMentionsFirstRequestLimit = 10;
constexpr auto kUnreadMentionsNextRequestLimit = 100;
constexpr auto kSharedMediaLimit = 100;
constexpr auto kReadFeaturedSetsTimeout = TimeMs(1000);

} // namespace

ApiWrap::ApiWrap(not_null<AuthSession*> session)
: _session(session)
, _messageDataResolveDelayed([this] { resolveMessageDatas(); })
, _webPagesTimer([this] { resolveWebPages(); })
, _draftsSaveTimer([this] { saveDraftsToCloud(); })
, _featuredSetsReadTimer([this] { readFeaturedSets(); }) {
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
		addLocalAlphaChangelog(1001024, "\xE2\x80\x94 Radically improved navigation. New side panel on the right with quick access to shared media and group members.\n\xE2\x80\x94 Pinned Messages. If you are a channel admin, pin messages to focus your subscribers\xE2\x80\x99 attention on important announcements.\n\xE2\x80\x94 Also supported clearing history in supergroups and added a host of minor improvements.");
		addLocalAlphaChangelog(1001026, "\xE2\x80\x94 Admin badges in supergroup messages.\n\xE2\x80\x94 Fix crashing on launch in OS X 10.6.\n\xE2\x80\x94 Bug fixes and other minor improvements.");
		addLocalAlphaChangelog(1001027, "\xE2\x80\x94 Saved Messages. Bookmark messages by forwarding them to \xE2\x80\x9C""Saved Messages\xE2\x80\x9D. Access them from the Chats list or from the side menu.");
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
		auto requestId = request(MTPmessages_GetMessages(
			MTP_vector<MTPint>(ids)
		)).done([this](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotMessageDatas(nullptr, result, requestId);
		}).fail([this](const RPCError &error, mtpRequestId requestId) {
			finalizeMessageDataRequest(nullptr, requestId);
		}).afterDelay(kSmallDelayMs).send();
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
			auto channel = j.key();
			auto requestId = request(MTPchannels_GetMessages(
				j.key()->inputChannel,
				MTP_vector<MTPint>(ids)
			)).done([=](const MTPmessages_Messages &result, mtpRequestId requestId) {
				gotMessageDatas(channel, result, requestId);
			}).fail([=](const RPCError &error, mtpRequestId requestId) {
				finalizeMessageDataRequest(channel, requestId);
			}).afterDelay(kSmallDelayMs).send();

			for (auto &request : *j) {
				if (request.requestId > 0) continue;
				request.requestId = requestId;
			}
		}
		++j;
	}
}

void ApiWrap::gotMessageDatas(ChannelData *channel, const MTPmessages_Messages &msgs, mtpRequestId requestId) {
	auto handleResult = [&](auto &&result) {
		App::feedUsers(result.vusers);
		App::feedChats(result.vchats);
		App::feedMsgs(result.vmessages, NewMessageExisting);
	};
	switch (msgs.type()) {
	case mtpc_messages_messages:
		handleResult(msgs.c_messages_messages());
		break;
	case mtpc_messages_messagesSlice:
		handleResult(msgs.c_messages_messagesSlice());
		break;
	case mtpc_messages_channelMessages: {
		auto &d = msgs.c_messages_channelMessages();
		if (channel) {
			channel->ptsReceived(d.vpts.v);
		} else {
			LOG(("App Error: received messages.channelMessages when no channel was passed! (ApiWrap::gotDependencyItem)"));
		}
		handleResult(d);
	} break;
	case mtpc_messages_messagesNotModified:
		LOG(("API Error: received messages.messagesNotModified! (ApiWrap::gotDependencyItem)"));
		break;
	}
	finalizeMessageDataRequest(channel, requestId);
}

void ApiWrap::finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId) {
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

	using UpdateFlag = Notify::PeerUpdate::Flag;
	if (auto chat = peer->asChat()) {
		if (d.vfull_chat.type() != mtpc_chatFull) {
			LOG(("MTP Error: bad type in gotChatFull for chat: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_chatFull();
		App::feedParticipants(f.vparticipants, false);
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
					fullPeerUpdated().notify(user);
				}
			} break;
			}
		}
		chat->setUserpicPhoto(f.vchat_photo);
		chat->setInviteLink((f.vexported_invite.type() == mtpc_chatInviteExported) ? qs(f.vexported_invite.c_chatInviteExported().vlink) : QString());
		chat->fullUpdated();

		notifySettingReceived(MTP_inputNotifyPeer(peer->input), f.vnotify_settings);
	} else if (auto channel = peer->asChannel()) {
		if (d.vfull_chat.type() != mtpc_channelFull) {
			LOG(("MTP Error: bad type in gotChatFull for channel: %1").arg(d.vfull_chat.type()));
			return;
		}
		auto &f = d.vfull_chat.c_channelFull();
		channel->setAvailableMinId(f.vavailable_min_id.v);
		auto canViewAdmins = channel->canViewAdmins();
		auto canViewMembers = channel->canViewMembers();
		auto canEditStickers = channel->canEditStickers();

		channel->setFullFlags(f.vflags.v);
		channel->setUserpicPhoto(f.vchat_photo);
		if (f.has_migrated_from_chat_id()) {
			channel->addFlags(MTPDchannel::Flag::f_megagroup);
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
			}
		}
		auto &v = f.vbot_info.v;
		for_const (auto &item, v) {
			switch (item.type()) {
			case mtpc_botInfo: {
				auto &b = item.c_botInfo();
				if (auto user = App::userLoaded(b.vuser_id.v)) {
					user->setBotInfo(item);
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
		ranges::overload([] {}, [](int a) {});
		if (f.has_pinned_msg_id()) {
			channel->setPinnedMessageId(f.vpinned_msg_id.v);
		} else {
			channel->clearPinnedMessage();
		}
		if (channel->isMegagroup()) {
			auto stickersChanged = (canEditStickers != channel->canEditStickers());
			auto stickerSet = (f.has_stickerset() ? &f.vstickerset.c_stickerSet() : nullptr);
			auto newSetId = (stickerSet ? stickerSet->vid.v : 0);
			auto oldSetId = (channel->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) ? channel->mgInfo->stickerSet.c_inputStickerSetID().vid.v : 0;
			if (oldSetId != newSetId) {
				channel->mgInfo->stickerSet = stickerSet ? MTP_inputStickerSetID(stickerSet->vid, stickerSet->vaccess_hash) : MTP_inputStickerSetEmpty();
				stickersChanged = true;
			}
			if (stickersChanged) {
				Notify::peerUpdatedDelayed(channel, UpdateFlag::ChannelStickersChanged);
			}
		}
		channel->fullUpdated();

		if (canViewAdmins != channel->canViewAdmins()
			|| canViewMembers != channel->canViewMembers()) {
			Notify::peerUpdatedDelayed(channel, UpdateFlag::ChannelRightsChanged);
		}

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

void ApiWrap::requestLastParticipants(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _participantsRequests.contains(channel)) {
		return;
	}

	const auto offset = 0;
	const auto participantsHash = 0;
	const auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_participantsRequests.remove(channel);
		parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyLastParticipantsList(
				channel,
				availableCount,
				list);
		});
	}).fail([this, channel](const RPCError &error) {
		_participantsRequests.remove(channel);
	}).send();

	_participantsRequests.insert(channel, requestId);
}

void ApiWrap::requestBots(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _botsRequests.contains(channel)) {
		return;
	}

	auto offset = 0;
	auto participantsHash = 0;
	auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsBots(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_botsRequests.remove(channel);
		parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyBotsList(
				channel,
				availableCount,
				list);
		});
	}).fail([this, channel](const RPCError &error) {
		_botsRequests.remove(channel);
	}).send();

	_botsRequests.insert(channel, requestId);
}

void ApiWrap::requestAdmins(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _adminsRequests.contains(channel)) {
		return;
	}

	auto offset = 0;
	auto participantsHash = 0;
	auto requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsAdmins(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this, channel](const MTPchannels_ChannelParticipants &result) {
		_adminsRequests.remove(channel);
		TLHelp::VisitChannelParticipants(result, base::overload([&](
				const MTPDchannels_channelParticipants &data) {
			App::feedUsers(data.vusers);
			applyAdminsList(
				channel,
				data.vcount.v,
				data.vparticipants.v);
		}, [&](mtpTypeId) {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		}));
	}).fail([this, channel](const RPCError &error) {
		_adminsRequests.remove(channel);
	}).send();

	_adminsRequests.insert(channel, requestId);
}

void ApiWrap::applyLastParticipantsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	channel->mgInfo->lastAdmins.clear();
	channel->mgInfo->lastRestricted.clear();
	channel->mgInfo->lastParticipants.clear();
	channel->mgInfo->lastParticipantsStatus = MegagroupInfo::LastParticipantsUpToDate;

	auto botStatus = channel->mgInfo->botStatus;
	const auto emptyAdminRights = MTP_channelAdminRights(MTP_flags(0));
	const auto emptyRestrictedRights = MTP_channelBannedRights(
		MTP_flags(0),
		MTP_int(0));
	for (const auto &p : list) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		const auto adminCanEdit = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().is_can_edit()
			: false;
		const auto adminRights = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().vadmin_rights
			: emptyAdminRights;
		const auto restrictedRights = (p.type() == mtpc_channelParticipantBanned)
			? p.c_channelParticipantBanned().vbanned_rights
			: emptyRestrictedRights;
		if (!userId) {
			continue;
		}

		auto user = App::user(userId);
		if (p.type() == mtpc_channelParticipantCreator) {
			channel->mgInfo->creator = user;
			if (!channel->mgInfo->admins.empty()
				&& !channel->mgInfo->admins.contains(userId)) {
				Data::ChannelAdminChanges changes(channel);
				changes.feed(userId, true);
			}
		}
		if (!base::contains(channel->mgInfo->lastParticipants, user)) {
			channel->mgInfo->lastParticipants.push_back(user);
			if (adminRights.c_channelAdminRights().vflags.v) {
				channel->mgInfo->lastAdmins.emplace(
					user,
					MegagroupInfo::Admin{ adminRights, adminCanEdit });
			} else if (restrictedRights.c_channelBannedRights().vflags.v != 0) {
				channel->mgInfo->lastRestricted.emplace(
					user,
					MegagroupInfo::Restricted{ restrictedRights });
			}
			if (user->botInfo) {
				channel->mgInfo->bots.insert(user);
				if (channel->mgInfo->botStatus != 0 && channel->mgInfo->botStatus < 2) {
					channel->mgInfo->botStatus = 2;
				}
			}
		}
	}
	//
	// getParticipants(Recent) sometimes can't return all members,
	// only some last subset, size of this subset is availableCount.
	//
	// So both list size and availableCount have nothing to do with
	// the full supergroup members count.
	//
	//if (list.isEmpty()) {
	//	channel->setMembersCount(channel->mgInfo->lastParticipants.size());
	//} else {
	//	channel->setMembersCount(availableCount);
	//}
	Notify::PeerUpdate update(channel);
	update.flags |= Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged;
	Notify::peerUpdatedDelayed(update);

	channel->mgInfo->botStatus = botStatus;
	if (App::main()) fullPeerUpdated().notify(channel);
}

void ApiWrap::applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	const auto history = App::historyLoaded(channel->id);
	channel->mgInfo->bots.clear();
	channel->mgInfo->botStatus = -1;

	auto needBotsInfos = false;
	auto botStatus = channel->mgInfo->botStatus;
	auto keyboardBotFound = !history || !history->lastKeyboardFrom;
	for (const auto &p : list) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		if (!userId) {
			continue;
		}

		auto user = App::user(userId);
		if (user->botInfo) {
			channel->mgInfo->bots.insert(user);
			botStatus = 2;// (botStatus > 0/* || !i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
			if (!user->botInfo->inited) {
				needBotsInfos = true;
			}
		}
		if (!keyboardBotFound && user->id == history->lastKeyboardFrom) {
			keyboardBotFound = true;
		}
	}
	if (needBotsInfos) {
		requestFullPeer(channel);
	}
	if (!keyboardBotFound) {
		history->clearLastKeyboard();
	}

	channel->mgInfo->botStatus = botStatus;
	if (App::main()) fullPeerUpdated().notify(channel);
}

void ApiWrap::applyAdminsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	auto admins = ranges::make_iterator_range(
		list.begin(), list.end()
	) | ranges::view::transform([](const MTPChannelParticipant &p) {
		return TLHelp::ReadChannelParticipantUserId(p);
	});
	auto adding = base::flat_set<UserId>{ admins.begin(), admins.end() };
	if (channel->mgInfo->creator) {
		adding.insert(peerToUser(channel->mgInfo->creator->id));
	}
	auto removing = channel->mgInfo->admins;

	if (removing.empty() && adding.empty()) {
		// Add some admin-placeholder so we don't DDOS
		// server with admins list requests.
		LOG(("API Error: Got empty admins list from server."));
		adding.insert(0);
	}

	Data::ChannelAdminChanges changes(channel);
	for (const auto addingId : adding) {
		if (!removing.remove(addingId)) {
			changes.feed(addingId, true);
		}
	}
	for (const auto removingId : removing) {
		changes.feed(removingId, false);
	}
}

void ApiWrap::requestSelfParticipant(ChannelData *channel) {
	if (_selfParticipantRequests.contains(channel)) {
		return;
	}

	auto requestId = request(MTPchannels_GetParticipant(
		channel->inputChannel,
		MTP_inputUserSelf()
	)).done([this, channel](const MTPchannels_ChannelParticipant &result) {
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
	}).afterDelay(kSmallDelayMs).send();

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

void ApiWrap::requestChannelMembersForAdd(
		not_null<ChannelData*> channel,
		base::lambda<void(const MTPchannels_ChannelParticipants&)> callback) {
	_channelMembersForAddCallback = std::move(callback);
	if (_channelMembersForAdd == channel) {
		return;
	}
	request(base::take(_channelMembersForAddRequestId)).cancel();

	auto offset = 0;
	auto participantsHash = 0;

	_channelMembersForAdd = channel;
	_channelMembersForAddRequestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(Global::ChatSizeMax()),
		MTP_int(participantsHash)
	)).done([this](const MTPchannels_ChannelParticipants &result) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback)(result);
	}).fail([this](const RPCError &error) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback);
	}).send();
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
		}).afterDelay(waitMs).send();
	}
}

void ApiWrap::saveStickerSets(const Stickers::Order &localOrder, const Stickers::Order &localRemoved) {
	for (auto requestId : base::take(_stickerSetDisenableRequests)) {
		request(requestId).cancel();
	}
	request(base::take(_stickersReorderRequestId)).cancel();
	request(base::take(_stickersClearRecentRequestId)).cancel();

	auto writeInstalled = true, writeRecent = false, writeCloudRecent = false, writeFaved = false, writeArchived = false;
	auto &recent = cGetRecentStickers();
	auto &sets = Auth().data().stickerSetsRef();

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
				}).afterDelay(kSmallDelayMs).send();

				_stickerSetDisenableRequests.insert(requestId);

				int removeIndex = Auth().data().stickerSetsOrder().indexOf(it->id);
				if (removeIndex >= 0) Auth().data().stickerSetsOrderRef().removeAt(removeIndex);
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

	auto &order = Auth().data().stickerSetsOrderRef();
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
				}).afterDelay(kSmallDelayMs).send();

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
	if (writeFaved) Local::writeFavedStickers();
	Auth().data().markStickersUpdated();

	if (_stickerSetDisenableRequests.empty()) {
		stickersSaveOrder();
	} else {
		requestSendDelayed();
	}
}

void ApiWrap::stickerSetDisenabled(mtpRequestId requestId) {
	_stickerSetDisenableRequests.remove(requestId);
	if (_stickerSetDisenableRequests.empty()) {
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
				Assert(item.type() == mtpc_contactStatus);
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

void ApiWrap::clearHistory(not_null<PeerData*> peer) {
	auto lastMsgId = MsgId(0);
	if (auto history = App::historyLoaded(peer->id)) {
		if (history->lastMsg) {
			lastMsgId = history->lastMsg->id;
			Local::addSavedPeer(history->peer, history->lastMsg->date);
		}
		history->clear();
		history->newLoaded = history->oldLoaded = true;
	}
	if (auto channel = peer->asChannel()) {
		if (auto migrated = peer->migrateFrom()) {
			clearHistory(migrated);
		}
		if (IsServerMsgId(lastMsgId)) {
			request(MTPchannels_DeleteHistory(
				channel->inputChannel,
				MTP_int(lastMsgId)
			)).send();
		}
	} else {
		request(MTPmessages_DeleteHistory(
			MTP_flags(MTPmessages_DeleteHistory::Flag::f_just_clear),
			peer->input,
			MTP_int(0)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			auto offset = applyAffectedHistory(peer, result);
			if (offset > 0) {
				clearHistory(peer);
			}
		}).send();
	}
}

int ApiWrap::applyAffectedHistory(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedHistory &result) {
	const auto &data = result.c_messages_affectedHistory();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	} else {
		App::main()->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	}
	return data.voffset.v;
}

void ApiWrap::applyAffectedMessages(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	} else {
		applyAffectedMessages(result);
	}
}

void ApiWrap::applyAffectedMessages(
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	App::main()->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
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

PeerData *ApiWrap::notifySettingReceived(
		MTPInputNotifyPeer notifyPeer,
		const MTPPeerNotifySettings &settings) {
	PeerData *requestedPeer = nullptr;
	switch (notifyPeer.type()) {
	case mtpc_inputNotifyAll:
		App::main()->applyNotifySetting(MTP_notifyAll(), settings);
	break;
	case mtpc_inputNotifyUsers:
		App::main()->applyNotifySetting(MTP_notifyUsers(), settings);
	break;
	case mtpc_inputNotifyChats:
		App::main()->applyNotifySetting(MTP_notifyChats(), settings);
	break;
	case mtpc_inputNotifyPeer: {
		auto &peer = notifyPeer.c_inputNotifyPeer().vpeer;
		switch (peer.type()) {
		case mtpc_inputPeerEmpty: App::main()->applyNotifySetting(
			MTP_notifyPeer(MTP_peerUser(MTP_int(0))),
			settings);
		break;
		case mtpc_inputPeerSelf: requestedPeer = App::self(); break;
		case mtpc_inputPeerUser: requestedPeer = App::user(peerFromUser(peer.c_inputPeerUser().vuser_id)); break;
		case mtpc_inputPeerChat: requestedPeer = App::chat(peerFromChat(peer.c_inputPeerChat().vchat_id)); break;
		case mtpc_inputPeerChannel: requestedPeer = App::channel(peerFromChannel(peer.c_inputPeerChannel().vchannel_id)); break;
		}
		if (requestedPeer) {
			App::main()->applyNotifySetting(
				MTP_notifyPeer(peerToMTP(requestedPeer->id)),
				settings);
		}
	} break;
	}
	_session->notifications().checkDelayed();
	return requestedPeer;
}

void ApiWrap::gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result) {
	_stickerSetRequests.remove(setId);
	Stickers::FeedSetFull(result);
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
		}).afterDelay(kSmallDelayMs).send();
	}
	QVector<mtpRequestId> reqsByIndex(idsByChannel.size(), 0);
	for (auto i = idsByChannel.cbegin(), e = idsByChannel.cend(); i != e; ++i) {
		reqsByIndex[i.value().first] = request(MTPchannels_GetMessages(i.key()->inputChannel, MTP_vector<MTPint>(i.value().second))).done([this, channel = i.key()](const MTPmessages_Messages &result, mtpRequestId requestId) {
			gotWebPages(channel, result, requestId);
		}).afterDelay(kSmallDelayMs).send();
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

void ApiWrap::requestParticipantsCountDelayed(
		not_null<ChannelData*> channel) {
	_participantsCountRequestTimer.call(
		kReloadChannelMembersTimeout,
		[this, channel] { channel->updateFullForced(); });
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

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (ApiWrap::gotWebPages)"));
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
			Auth().data().setLastStickersUpdate(0);
			updateStickers();
		}).send();
	}
}

void ApiWrap::updateStickers() {
	auto now = getms(true);
	requestStickers(now);
	requestRecentStickers(now);
	requestFavedStickers(now);
	requestFeaturedStickers(now);
	requestSavedGifs(now);
}

void ApiWrap::setGroupStickerSet(not_null<ChannelData*> megagroup, const MTPInputStickerSet &set) {
	Expects(megagroup->mgInfo != nullptr);
	megagroup->mgInfo->stickerSet = set;
	request(MTPchannels_SetStickers(megagroup->inputChannel, set)).send();
	Auth().data().markStickersUpdated();
}

void ApiWrap::requestStickers(TimeId now) {
	if (!Auth().data().stickersUpdateNeeded(now)
		|| _stickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_AllStickers &result) {
		Auth().data().setLastStickersUpdate(getms(true));
		_stickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_allStickersNotModified: return;
		case mtpc_messages_allStickers: {
			auto &d = result.c_messages_allStickers();
			Stickers::SetsReceived(d.vsets.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::stickersDone()");
		}
	};
	_stickersUpdateRequest = request(MTPmessages_GetAllStickers(MTP_int(Local::countStickersHash(true)))).done(onDone).fail([this, onDone](const RPCError &error) {
		LOG(("App Fail: Failed to get stickers!"));
		onDone(MTP_messages_allStickersNotModified());
	}).send();
}

void ApiWrap::requestRecentStickers(TimeId now) {
	if (!Auth().data().recentStickersUpdateNeeded(now)
		|| _recentStickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_RecentStickers &result) {
		Auth().data().setLastRecentStickersUpdate(getms(true));
		_recentStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_recentStickersNotModified: return;
		case mtpc_messages_recentStickers: {
			auto &d = result.c_messages_recentStickers();
			Stickers::SpecialSetReceived(Stickers::CloudRecentSetId, lang(lng_recent_stickers), d.vstickers.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::recentStickersDone()");
		}
	};
	_recentStickersUpdateRequest = request(MTPmessages_GetRecentStickers(MTP_flags(0), MTP_int(Local::countRecentStickersHash()))).done(onDone).fail([this, onDone](const RPCError &error) {
		LOG(("App Fail: Failed to get recent stickers!"));
		onDone(MTP_messages_recentStickersNotModified());
	}).send();
}

void ApiWrap::requestFavedStickers(TimeId now) {
	if (!Auth().data().favedStickersUpdateNeeded(now)
		|| _favedStickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_FavedStickers &result) {
		Auth().data().setLastFavedStickersUpdate(getms(true));
		_favedStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_favedStickersNotModified: return;
		case mtpc_messages_favedStickers: {
			auto &d = result.c_messages_favedStickers();
			Stickers::SpecialSetReceived(Stickers::FavedSetId, Lang::Hard::FavedSetTitle(), d.vstickers.v, d.vhash.v, d.vpacks.v);
		} return;
		default: Unexpected("Type in ApiWrap::favedStickersDone()");
		}
	};
	_favedStickersUpdateRequest = request(MTPmessages_GetFavedStickers(MTP_int(Local::countFavedStickersHash()))).done(onDone).fail([this, onDone](const RPCError &error) {
		LOG(("App Fail: Failed to get faved stickers!"));
		onDone(MTP_messages_favedStickersNotModified());
	}).send();
}

void ApiWrap::requestFeaturedStickers(TimeId now) {
	if (!Auth().data().featuredStickersUpdateNeeded(now)
		|| _featuredStickersUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_FeaturedStickers &result) {
		Auth().data().setLastFeaturedStickersUpdate(getms(true));
		_featuredStickersUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_featuredStickersNotModified: return;
		case mtpc_messages_featuredStickers: {
			auto &d = result.c_messages_featuredStickers();
			Stickers::FeaturedSetsReceived(d.vsets.v, d.vunread.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::featuredStickersDone()");
		}
	};
	_featuredStickersUpdateRequest = request(MTPmessages_GetFeaturedStickers(MTP_int(Local::countFeaturedStickersHash()))).done(onDone).fail([this, onDone](const RPCError &error) {
		LOG(("App Fail: Failed to get featured stickers!"));
		onDone(MTP_messages_featuredStickersNotModified());
	}).send();
}

void ApiWrap::requestSavedGifs(TimeId now) {
	if (!Auth().data().savedGifsUpdateNeeded(now)
		|| _savedGifsUpdateRequest) {
		return;
	}
	auto onDone = [this](const MTPmessages_SavedGifs &result) {
		Auth().data().setLastSavedGifsUpdate(getms(true));
		_savedGifsUpdateRequest = 0;

		switch (result.type()) {
		case mtpc_messages_savedGifsNotModified: return;
		case mtpc_messages_savedGifs: {
			auto &d = result.c_messages_savedGifs();
			Stickers::GifsReceived(d.vgifs.v, d.vhash.v);
		} return;
		default: Unexpected("Type in ApiWrap::savedGifsDone()");
		}
	};
	_savedGifsUpdateRequest = request(MTPmessages_GetSavedGifs(MTP_int(Local::countSavedGifsHash()))).done(onDone).fail([this, onDone](const RPCError &error) {
		LOG(("App Fail: Failed to get saved gifs!"));
		onDone(MTP_messages_savedGifsNotModified());
	}).send();
}

void ApiWrap::readFeaturedSetDelayed(uint64 setId) {
	if (!_featuredSetsRead.contains(setId)) {
		_featuredSetsRead.insert(setId);
		_featuredSetsReadTimer.callOnce(kReadFeaturedSetsTimeout);
	}
}

void ApiWrap::readFeaturedSets() {
	auto &sets = Auth().data().stickerSetsRef();
	auto count = Auth().data().featuredStickerSetsUnreadCount();
	QVector<MTPlong> wrappedIds;
	wrappedIds.reserve(_featuredSetsRead.size());
	for (auto setId : _featuredSetsRead) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
			wrappedIds.append(MTP_long(setId));
			if (count) {
				--count;
			}
		}
	}
	_featuredSetsRead.clear();

	if (!wrappedIds.empty()) {
		auto requestData = MTPmessages_ReadFeaturedStickers(
			MTP_vector<MTPlong>(wrappedIds));
		request(std::move(requestData)).done([](const MTPBool &result) {
			Local::writeFeaturedStickers();
			Auth().data().markStickersUpdated();
		}).send();

		Auth().data().setFeaturedStickerSetsUnreadCount(count);
	}
}

void ApiWrap::parseChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		base::lambda<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		base::lambda<void()> callbackNotModified) {
	TLHelp::VisitChannelParticipants(result, base::overload([&](
			const MTPDchannels_channelParticipants &data) {
		App::feedUsers(data.vusers);
		if (channel->mgInfo) {
			refreshChannelAdmins(channel, data.vparticipants.v);
		}
		if (callbackList) {
			callbackList(data.vcount.v, data.vparticipants.v);
		}
	}, [&](mtpTypeId) {
		if (callbackNotModified) {
			callbackNotModified();
		} else {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		}
	}));
}

void ApiWrap::refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants) {
	Data::ChannelAdminChanges changes(channel);
	for (auto &p : participants) {
		const auto userId = TLHelp::ReadChannelParticipantUserId(p);
		const auto isAdmin = (p.type() == mtpc_channelParticipantAdmin)
			|| (p.type() == mtpc_channelParticipantCreator);
		changes.feed(userId, isAdmin);
	}
}

void ApiWrap::parseRecentChannelParticipants(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		base::lambda<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		base::lambda<void()> callbackNotModified) {
	parseChannelParticipants(channel, result, [&](
			int availableCount,
			const QVector<MTPChannelParticipant> &list) {
		auto applyLast = channel->isMegagroup()
			&& (channel->mgInfo->lastParticipants.size() <= list.size());
		if (applyLast) {
			applyLastParticipantsList(
				channel,
				availableCount,
				list);
		}
		callbackList(availableCount, list);
	}, std::move(callbackNotModified));
}

void ApiWrap::applyUpdatesNoPtsCheck(const MTPUpdates &updates) {
	switch (updates.type()) {
	case mtpc_updateShortMessage: {
		auto &d = updates.c_updateShortMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		App::histories().addNewMessage(MTP_message(
			MTP_flags(flags),
			d.vid,
			d.is_out() ? MTP_int(Auth().userId()) : d.vuser_id,
			MTP_peerUser(d.is_out() ? d.vuser_id : MTP_int(Auth().userId())),
			d.vfwd_from,
			d.vvia_bot_id,
			d.vreply_to_msg_id,
			d.vdate,
			d.vmessage,
			MTP_messageMediaEmpty(),
			MTPnullMarkup,
			d.has_entities() ? d.ventities : MTPnullEntities,
			MTPint(),
			MTPint(),
			MTPstring(),
			MTPlong()), NewMessageUnread);
	} break;

	case mtpc_updateShortChatMessage: {
		auto &d = updates.c_updateShortChatMessage();
		auto flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		App::histories().addNewMessage(MTP_message(
			MTP_flags(flags),
			d.vid,
			d.vfrom_id,
			MTP_peerChat(d.vchat_id),
			d.vfwd_from,
			d.vvia_bot_id,
			d.vreply_to_msg_id,
			d.vdate,
			d.vmessage,
			MTP_messageMediaEmpty(),
			MTPnullMarkup,
			d.has_entities() ? d.ventities : MTPnullEntities,
			MTPint(),
			MTPint(),
			MTPstring(),
			MTPlong()), NewMessageUnread);
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
		auto possiblyReadMentions = base::flat_set<MsgId>();
		for_const (auto &msgId, d.vmessages.v) {
			if (auto item = App::histItemById(NoChannel, msgId.v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					Auth().data().requestItemRepaint(item);

					if (item->out() && item->history()->peer->isUser()) {
						auto when = App::main()->requestingDifference() ? 0 : unixtime();
						item->history()->peer->asUser()->madeAction(when);
					}
				}
			} else {
				// Perhaps it was an unread mention!
				possiblyReadMentions.insert(msgId.v);
			}
		}
		checkForUnreadMentions(possiblyReadMentions);
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

template <typename Callback>
void ApiWrap::requestMessageAfterDate(
		not_null<PeerData*> peer,
		const QDate &date,
		Callback &&callback) {
	// API returns a message with date <= offset_date.
	// So we request a message with offset_date = desired_date - 1 and add_offset = -1.
	// This should give us the first message with date >= desired_date.
	auto offsetId = 0;
	auto offsetDate = static_cast<int>(QDateTime(date).toTime_t()) - 1;
	auto addOffset = -1;
	auto limit = 1;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;
	request(MTPmessages_GetHistory(
		peer->input,
		MTP_int(offsetId),
		MTP_int(offsetDate),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId),
		MTP_int(historyHash)
	)).done([
		peer,
		offsetDate,
		callback = std::forward<Callback>(callback)
	](const MTPmessages_Messages &result) {
		auto getMessagesList = [&result, peer]() -> const QVector<MTPMessage>* {
			auto handleMessages = [](auto &messages) {
				App::feedUsers(messages.vusers);
				App::feedChats(messages.vchats);
				return &messages.vmessages.v;
			};
			switch (result.type()) {
			case mtpc_messages_messages:
				return handleMessages(result.c_messages_messages());
			case mtpc_messages_messagesSlice:
				return handleMessages(result.c_messages_messagesSlice());
			case mtpc_messages_channelMessages: {
				auto &messages = result.c_messages_channelMessages();
				if (peer && peer->isChannel()) {
					peer->asChannel()->ptsReceived(messages.vpts.v);
				} else {
					LOG(("API Error: received messages.channelMessages when no channel was passed! (ApiWrap::jumpToDate)"));
				}
				return handleMessages(messages);
			} break;
			case mtpc_messages_messagesNotModified: {
				LOG(("API Error: received messages.messagesNotModified! (ApiWrap::jumpToDate)"));
			} break;
			}
			return nullptr;
		};

		if (auto list = getMessagesList()) {
			App::feedMsgs(*list, NewMessageExisting);
			for (auto &message : *list) {
				if (dateFromMessage(message) >= offsetDate) {
					callback(idFromMessage(message));
					return;
				}
			}
		}
		callback(ShowAtUnreadMsgId);
	}).send();
}

void ApiWrap::jumpToDate(not_null<PeerData*> peer, const QDate &date) {
	if (auto channel = peer->migrateTo()) {
		jumpToDate(channel, date);
		return;
	}
	auto jumpToDateInPeer = [peer, date, this] {
		requestMessageAfterDate(peer, date, [peer](MsgId resultId) {
			Ui::showPeerHistory(peer, resultId);
		});
	};
	if (auto chat = peer->migrateFrom()) {
		requestMessageAfterDate(chat, date, [=](MsgId resultId) {
			if (resultId) {
				Ui::showPeerHistory(chat, resultId);
			} else {
				jumpToDateInPeer();
			}
		});
	} else {
		jumpToDateInPeer();
	}
}

void ApiWrap::preloadEnoughUnreadMentions(not_null<History*> history) {
	auto fullCount = history->getUnreadMentionsCount();
	auto loadedCount = history->getUnreadMentionsLoadedCount();
	auto allLoaded = (fullCount >= 0) ? (loadedCount >= fullCount) : false;
	if (fullCount < 0 || loadedCount >= kUnreadMentionsPreloadIfLess || allLoaded) {
		return;
	}
	if (_unreadMentionsRequests.contains(history)) {
		return;
	}
	auto offsetId = loadedCount ? history->getMaxLoadedUnreadMention() : 1;
	auto limit = loadedCount ? kUnreadMentionsNextRequestLimit : kUnreadMentionsFirstRequestLimit;
	auto addOffset = loadedCount ? -(limit + 1) : -limit;
	auto maxId = 0;
	auto minId = 0;
	auto requestId = request(MTPmessages_GetUnreadMentions(history->peer->input, MTP_int(offsetId), MTP_int(addOffset), MTP_int(limit), MTP_int(maxId), MTP_int(minId))).done([this, history](const MTPmessages_Messages &result) {
		_unreadMentionsRequests.remove(history);
		history->addUnreadMentionsSlice(result);
	}).fail([this, history](const RPCError &error) {
		_unreadMentionsRequests.remove(history);
	}).send();
	_unreadMentionsRequests.emplace(history, requestId);
}

void ApiWrap::checkForUnreadMentions(const base::flat_set<MsgId> &possiblyReadMentions, ChannelData *channel) {
	for (auto msgId : possiblyReadMentions) {
		requestMessageData(channel, msgId, [](ChannelData *channel, MsgId msgId) {
			if (auto item = App::histItemById(channel, msgId)) {
				if (item->mentionsMe()) {
					item->markMediaRead();
				}
			}
		});
	}
}

void ApiWrap::cancelEditChatAdmins(not_null<ChatData*> chat) {
	_chatAdminsEnabledRequests.take(
		chat
	) | requestCanceller();

	_chatAdminsSaveRequests.take(
		chat
	) | [&](auto &&requests) {
		ranges::for_each(std::move(requests), requestCanceller());
	};

	_chatAdminsToSave.remove(chat);
}

void ApiWrap::editChatAdmins(
		not_null<ChatData*> chat,
		bool adminsEnabled,
		base::flat_set<not_null<UserData*>> &&admins) {
	cancelEditChatAdmins(chat);
	if (adminsEnabled) {
		_chatAdminsToSave.emplace(chat, std::move(admins));
	}

	auto requestId = request(MTPmessages_ToggleChatAdmins(chat->inputChat, MTP_bool(adminsEnabled))).done([this, chat](const MTPUpdates &updates) {
		_chatAdminsEnabledRequests.remove(chat);
		applyUpdates(updates);
		saveChatAdmins(chat);
	}).fail([this, chat](const RPCError &error) {
		_chatAdminsEnabledRequests.remove(chat);
		if (error.type() == qstr("CHAT_NOT_MODIFIED")) {
			saveChatAdmins(chat);
		}
	}).send();
	_chatAdminsEnabledRequests.emplace(chat, requestId);
}

void ApiWrap::saveChatAdmins(not_null<ChatData*> chat) {
	if (!_chatAdminsToSave.contains(chat)) {
		return;
	}
	auto requestId = request(MTPmessages_GetFullChat(chat->inputChat)).done([this, chat](const MTPmessages_ChatFull &result) {
		_chatAdminsEnabledRequests.remove(chat);
		processFullPeer(chat, result);
		sendSaveChatAdminsRequests(chat);
	}).fail([this, chat](const RPCError &error) {
		_chatAdminsEnabledRequests.remove(chat);
		_chatAdminsToSave.remove(chat);
	}).send();
	_chatAdminsEnabledRequests.emplace(chat, requestId);
}

void ApiWrap::sendSaveChatAdminsRequests(not_null<ChatData*> chat) {
	auto editOne = [this, chat](not_null<UserData*> user, bool admin) {
		auto requestId = request(MTPmessages_EditChatAdmin(
				chat->inputChat,
				user->inputUser,
				MTP_bool(admin)))
			.done([this, chat, user, admin](
					const MTPBool &result,
					mtpRequestId requestId) {
			_chatAdminsSaveRequests[chat].remove(requestId);
			if (_chatAdminsSaveRequests[chat].empty()) {
				_chatAdminsSaveRequests.remove(chat);
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::AdminsChanged);
			}
			if (mtpIsTrue(result)) {
				if (admin) {
					if (chat->noParticipantInfo()) {
						requestFullPeer(chat);
					} else {
						chat->admins.insert(user);
					}
				} else {
					chat->admins.remove(user);
				}
			}
		}).fail([this, chat](
				const RPCError &error,
				mtpRequestId requestId) {
			_chatAdminsSaveRequests[chat].remove(requestId);
			if (_chatAdminsSaveRequests[chat].empty()) {
				_chatAdminsSaveRequests.remove(chat);
			}
			chat->invalidateParticipants();
			if (error.type() == qstr("USER_RESTRICTED")) {
				Ui::show(Box<InformBox>(lang(lng_cant_do_this)));
			}
		}).afterDelay(kSmallDelayMs).send();

		_chatAdminsSaveRequests[chat].insert(requestId);
	};
	auto appointOne = [&](auto user) { editOne(user, true); };
	auto removeOne = [&](auto user) { editOne(user, false); };

	auto admins = _chatAdminsToSave.take(chat);
	Assert(!!admins);

	auto toRemove = chat->admins;
	auto toAppoint = std::vector<not_null<UserData*>>();
	if (!admins->empty()) {
		toAppoint.reserve(admins->size());
		for (auto user : *admins) {
			if (!toRemove.remove(user) && user->id != peerFromUser(chat->creator)) {
				toAppoint.push_back(user);
			}
		}
	}
	ranges::for_each(toRemove, removeOne);
	ranges::for_each(toAppoint, appointOne);
	requestSendDelayed();
}

void ApiWrap::requestSharedMediaCount(
		not_null<PeerData*> peer,
		Storage::SharedMediaType type) {
	requestSharedMedia(peer, type, 0, SliceType::Before);
}

void ApiWrap::requestSharedMedia(
		not_null<PeerData*> peer,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice) {
	auto key = std::make_tuple(peer, type, messageId, slice);
	if (_sharedMediaRequests.contains(key)) {
		return;
	}

	auto prepared = Api::PrepareSearchRequest(
		peer,
		type,
		QString(),
		messageId,
		slice);
	if (prepared.vfilter.type() == mtpc_inputMessagesFilterEmpty) {
		return;
	}

	auto requestId = request(
		std::move(prepared)
	).done([this, peer, type, messageId, slice](
			const MTPmessages_Messages &result) {
		auto key = std::make_tuple(peer, type, messageId, slice);
		_sharedMediaRequests.remove(key);
		sharedMediaDone(peer, type, messageId, slice, result);
	}).fail([this, key](const RPCError &error) {
		_sharedMediaRequests.remove(key);
	}).send();
	_sharedMediaRequests.emplace(key, requestId);
}

void ApiWrap::sharedMediaDone(
		not_null<PeerData*> peer,
		SharedMediaType type,
		MsgId messageId,
		SliceType slice,
		const MTPmessages_Messages &result) {
	auto parsed = Api::ParseSearchResult(
		peer,
		type,
		messageId,
		slice,
		result);
	Auth().storage().add(Storage::SharedMediaAddSlice(
		peer->id,
		type,
		std::move(parsed.messageIds),
		parsed.noSkipRange,
		parsed.fullCount
	));
}

void ApiWrap::requestUserPhotos(
		not_null<UserData*> user,
		PhotoId afterId) {
	if (_userPhotosRequests.contains(user)) {
		return;
	}

	auto limit = kSharedMediaLimit;

	auto requestId = request(MTPphotos_GetUserPhotos(
		user->inputUser,
		MTP_int(0),
		MTP_long(afterId),
		MTP_int(limit)
	)).done([this, user, afterId](const MTPphotos_Photos &result) {
		_userPhotosRequests.remove(user);
		userPhotosDone(user, afterId, result);
	}).fail([this, user](const RPCError &error) {
		_userPhotosRequests.remove(user);
	}).send();
	_userPhotosRequests.emplace(user, requestId);
}

void ApiWrap::userPhotosDone(
		not_null<UserData*> user,
		PhotoId photoId,
		const MTPphotos_Photos &result) {
	auto fullCount = 0;
	auto &photos = *[&] {
		switch (result.type()) {
		case mtpc_photos_photos: {
			auto &d = result.c_photos_photos();
			App::feedUsers(d.vusers);
			fullCount = d.vphotos.v.size();
			return &d.vphotos.v;
		} break;

		case mtpc_photos_photosSlice: {
			auto &d = result.c_photos_photosSlice();
			App::feedUsers(d.vusers);
			fullCount = d.vcount.v;
			return &d.vphotos.v;
		} break;
		}
		Unexpected("photos.Photos type in userPhotosDone()");
	}();

	auto photoIds = std::vector<PhotoId>();
	photoIds.reserve(photos.size());
	for (auto &photo : photos) {
		if (auto photoData = App::feedPhoto(photo)) {
			photoIds.push_back(photoData->id);
		}
	}
	Auth().storage().add(Storage::UserPhotosAddSlice(
		user->id,
		std::move(photoIds),
		fullCount
	));
}

void ApiWrap::sendAction(const SendOptions &options) {
	readServerHistory(options.history);
	options.history->getReadyFor(ShowAtTheEndMsgId);
	_sendActions.fire_copy(options);
}

void ApiWrap::forwardMessages(
		HistoryItemsList &&items,
		const SendOptions &options,
		base::lambda_once<void()> &&successCallback) {
	Expects(!items.empty());

	struct SharedCallback {
		int requestsLeft = 0;
		base::lambda_once<void()> callback;
	};
	const auto shared = successCallback
		? std::make_shared<SharedCallback>()
		: std::shared_ptr<SharedCallback>();
	if (successCallback) {
		shared->callback = std::move(successCallback);
	}

	const auto count = int(items.size());
	const auto genClientSideMessage = options.generateLocal && (count < 2);
	const auto history = options.history;

	readServerHistory(history);

	const auto channelPost = history->peer->isChannel()
		&& !history->peer->isMegagroup();
	const auto silentPost = channelPost && options.silent;

	auto flags = MTPDmessage::Flags(0);
	auto sendFlags = MTPmessages_ForwardMessages::Flags(0);
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (history->peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_ForwardMessages::Flag::f_silent;
	}

	auto forwardFrom = items.front()->history()->peer;
	auto ids = QVector<MTPint>();
	auto randomIds = QVector<MTPlong>();

	const auto sendAccumulated = [&] {
		if (shared) {
			++shared->requestsLeft;
		}
		history->sendRequestId = request(MTPmessages_ForwardMessages(
			MTP_flags(sendFlags),
			forwardFrom->input,
			MTP_vector<MTPint>(ids),
			MTP_vector<MTPlong>(randomIds),
			history->peer->input
		)).done([=, callback = std::move(successCallback)](
				const MTPUpdates &updates) {
			applyUpdates(updates);
			if (shared && !--shared->requestsLeft) {
				shared->callback();
			}
		}).afterRequest(
			history->sendRequestId
		).send();

		ids.resize(0);
		randomIds.resize(0);
	};

	ids.reserve(count);
	randomIds.reserve(count);
	for (const auto item : items) {
		auto randomId = rand_value<uint64>();
		if (genClientSideMessage) {
			if (auto message = item->toHistoryMessage()) {
				const auto newId = FullMsgId(
					peerToChannel(history->peer->id),
					clientMsgId());
				const auto self = Auth().user();
				const auto messageFromId = channelPost
					? UserId(0)
					: peerToUser(self->id);
				const auto messagePostAuthor = channelPost
					? (self->firstName + ' ' + self->lastName)
					: QString();
				history->addNewForwarded(
					newId.msg,
					flags,
					date(MTP_int(unixtime())),
					messageFromId,
					messagePostAuthor,
					message);
				App::historyRegRandom(randomId, newId);
			}
		}
		if (forwardFrom != item->history()->peer) {
			sendAccumulated();
			forwardFrom = item->history()->peer;
		}
		ids.push_back(MTP_int(item->id));
		randomIds.push_back(MTP_long(randomId));
	}
	sendAccumulated();
}

void ApiWrap::shareContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		const SendOptions &options) {
	const auto userId = UserId(0);
	sendSharedContact(phone, firstName, lastName, userId, options);
}

void ApiWrap::shareContact(
		not_null<UserData*> user,
		const SendOptions &options) {
	const auto userId = peerToUser(user->id);
	const auto phone = user->phone().isEmpty()
		? App::phoneFromSharedContact(userId)
		: user->phone();
	if (phone.isEmpty()) {
		return;
	}
	sendSharedContact(
		phone,
		user->firstName,
		user->lastName,
		userId,
		options);
}

void ApiWrap::sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendOptions &options) {
	sendAction(options);

	const auto history = options.history;
	const auto peer = history->peer;

	const auto randomId = rand_value<uint64>();
	const auto newId = FullMsgId(history->channelId(), clientMsgId());
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = channelPost && options.silent;

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;

	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
		if (peer->asChannel()->addsSignature()) {
			flags |= MTPDmessage::Flag::f_post_author;
		}
	} else {
		flags |= MTPDmessage::Flag::f_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto messageFromId = channelPost ? 0 : Auth().userId();
	const auto messagePostAuthor = channelPost
		? (Auth().user()->firstName + ' ' + Auth().user()->lastName)
		: QString();
	history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(peer->id),
			MTPnullFwdHeader,
			MTPint(),
			MTP_int(options.replyTo),
			MTP_int(unixtime()),
			MTP_string(""),
			MTP_messageMediaContact(
				MTP_string(phone),
				MTP_string(firstName),
				MTP_string(lastName),
				MTP_int(userId)),
			MTPnullMarkup,
			MTPnullEntities,
			MTP_int(1),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTPlong()),
		NewMessageUnread);

	history->sendRequestId = request(MTPmessages_SendMedia(
		MTP_flags(sendFlags),
		peer->input,
		MTP_int(options.replyTo),
		MTP_inputMediaContact(
			MTP_string(phone),
			MTP_string(firstName),
			MTP_string(lastName)),
		MTP_long(randomId),
		MTPnullMarkup
	)).done([=](const MTPUpdates &result) {
		applyUpdates(result);
	}).fail([](const RPCError &error) {
		if (error.type() == qstr("PEER_FLOOD")) {
			Ui::show(Box<InformBox>(
				PeerFloodErrorText(PeerFloodType::Send)));
		} else if (error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
			const auto link = textcmdLink(
				Messenger::Instance().createInternalLinkFull(qsl("spambot")),
				lang(lng_cant_more_info));
			Ui::show(Box<InformBox>(lng_error_public_groups_denied(
				lt_more_info,
				link)));
		}
	}).afterRequest(
		history->sendRequestId
	).send();

	App::historyRegRandom(randomId, newId);
}

void ApiWrap::readServerHistory(not_null<History*> history) {
	if (history->unreadCount()) {
		readServerHistoryForce(history);
	}
}

void ApiWrap::readServerHistoryForce(not_null<History*> history) {
	const auto peer = history->peer;
	const auto upTo = history->inboxRead(0);
	if (const auto channel = peer->asChannel()) {
		if (!channel->amIn()) {
			return; // no read request for channels that I didn't join
		} else if (const auto migrateFrom = channel->migrateFrom()) {
			if (const auto migrated = App::historyLoaded(migrateFrom)) {
				readServerHistory(migrated);
			}
		}
	}

	if (_readRequests.contains(peer)) {
		const auto i = _readRequestsPending.find(peer);
		if (i == _readRequestsPending.cend()) {
			_readRequestsPending.emplace(peer, upTo);
		} else if (i->second < upTo) {
			i->second = upTo;
		}
	} else {
		sendReadRequest(peer, upTo);
	}
}

void ApiWrap::sendReadRequest(not_null<PeerData*> peer, MsgId upTo) {
	const auto requestId = [&] {
		const auto finished = [=] {
			_readRequests.remove(peer);
			if (const auto next = _readRequestsPending.take(peer)) {
				sendReadRequest(peer, *next);
			}
		};
		if (const auto channel = peer->asChannel()) {
			return request(MTPchannels_ReadHistory(
				channel->inputChannel,
				MTP_int(upTo)
			)).done([=](const MTPBool &result) {
				finished();
			}).fail([=](const RPCError &error) {
				finished();
			}).send();
		}
		return request(MTPmessages_ReadHistory(
			peer->input,
			MTP_int(upTo)
		)).done([=](const MTPmessages_AffectedMessages &result) {
			applyAffectedMessages(peer, result);
			finished();
		}).fail([=](const RPCError &error) {
			finished();
		}).send();
	}();
	_readRequests.emplace(peer, requestId, upTo);
}

ApiWrap::~ApiWrap() = default;
