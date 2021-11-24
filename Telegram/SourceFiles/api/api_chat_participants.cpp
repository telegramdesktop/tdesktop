/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_participants.h"

#include "apiwrap.h"
#include "boxes/add_contact_box.h" // ShowAddParticipantsError
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_channel_admins.h"
#include "data/data_chat.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"

namespace Api {
namespace {

constexpr auto kSmallDelayMs = crl::time(5);

// 1 second wait before reload members in channel after adding.
constexpr auto kReloadChannelMembersTimeout = 1000;

// Max users in one super group invite request.
constexpr auto kMaxUsersPerInvite = 100;

// How many messages from chat history server should forward to user,
// that was added to this chat.
constexpr auto kForwardMessagesOnAdd = 100;

void ApplyMegagroupAdmins(
		not_null<ChannelData*> channel,
		const MTPDchannels_channelParticipants &data) {
	Expects(channel->isMegagroup());

	channel->owner().processUsers(data.vusers());

	const auto &list = data.vparticipants().v;
	const auto i = ranges::find(
		list,
		mtpc_channelParticipantCreator,
		&MTPChannelParticipant::type);
	if (i != list.end()) {
		const auto &data = i->c_channelParticipantCreator();
		const auto userId = data.vuser_id().v;
		channel->mgInfo->creator = channel->owner().userLoaded(userId);
		channel->mgInfo->creatorRank = qs(data.vrank().value_or_empty());
	} else {
		channel->mgInfo->creator = nullptr;
		channel->mgInfo->creatorRank = QString();
	}

	auto adding = base::flat_map<UserId, QString>();
	auto admins = ranges::make_subrange(
		list.begin(), list.end()
	) | ranges::views::transform([](const MTPChannelParticipant &p) {
		const auto participantId = p.match([](
			const MTPDchannelParticipantBanned &data) {
			return peerFromMTP(data.vpeer());
		}, [](const MTPDchannelParticipantLeft &data) {
			return peerFromMTP(data.vpeer());
		}, [](const auto &data) {
			return peerFromUser(data.vuser_id());
		});
		const auto rank = p.match([](const MTPDchannelParticipantAdmin &data) {
			return qs(data.vrank().value_or_empty());
		}, [](const MTPDchannelParticipantCreator &data) {
			return qs(data.vrank().value_or_empty());
		}, [](const auto &data) {
			return QString();
		});
		return std::make_pair(participantId, rank);
	}) | ranges::views::filter([](const auto &pair) {
		return peerIsUser(pair.first);
	});
	for (const auto &[participantId, rank] : admins) {
		Assert(peerIsUser(participantId));
		adding.emplace(peerToUser(participantId), rank);
	}
	if (channel->mgInfo->creator) {
		adding.emplace(
			peerToUser(channel->mgInfo->creator->id),
			channel->mgInfo->creatorRank);
	}
	auto removing = channel->mgInfo->admins;
	if (removing.empty() && adding.empty()) {
		// Add some admin-placeholder so we don't DDOS
		// server with admins list requests.
		LOG(("API Error: Got empty admins list from server."));
		adding.emplace(0, QString());
	}

	Data::ChannelAdminChanges changes(channel);
	for (const auto &[addingId, rank] : adding) {
		if (!removing.remove(addingId)) {
			changes.add(addingId, rank);
		}
	}
	for (const auto &[removingId, rank] : removing) {
		changes.remove(removingId);
	}
}

} // namespace

ChatParticipants::ChatParticipants(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void ChatParticipants::requestForAdd(
		not_null<ChannelData*> channel,
		Fn<void(const MTPchannels_ChannelParticipants&)> callback) {
	_channelMembersForAddCallback = std::move(callback);
	if (_channelMembersForAdd == channel) {
		return;
	}
	_api.request(base::take(_channelMembersForAddRequestId)).cancel();

	const auto offset = 0;
	const auto participantsHash = uint64(0);

	_channelMembersForAdd = channel;
	_channelMembersForAddRequestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(_session->serverConfig().chatSizeMax),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback)(result);
	}).fail([=](const MTP::Error &error) {
		base::take(_channelMembersForAddRequestId);
		base::take(_channelMembersForAdd);
		base::take(_channelMembersForAddCallback);
	}).send();
}

void ChatParticipants::refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants) {
	Data::ChannelAdminChanges changes(channel);
	for (const auto &p : participants) {
		const auto participantId = p.match([](
				const MTPDchannelParticipantBanned &data) {
			return peerFromMTP(data.vpeer());
		}, [](const MTPDchannelParticipantLeft &data) {
			return peerFromMTP(data.vpeer());
		}, [](const auto &data) {
			return peerFromUser(data.vuser_id());
		});
		const auto userId = peerToUser(participantId);
		p.match([&](const MTPDchannelParticipantAdmin &data) {
			Assert(peerIsUser(participantId));
			changes.add(userId, qs(data.vrank().value_or_empty()));
		}, [&](const MTPDchannelParticipantCreator &data) {
			Assert(peerIsUser(participantId));
			const auto rank = qs(data.vrank().value_or_empty());
			if (const auto info = channel->mgInfo.get()) {
				info->creator = channel->owner().userLoaded(userId);
				info->creatorRank = rank;
			}
			changes.add(userId, rank);
		}, [&](const auto &data) {
			if (userId) {
				changes.remove(userId);
			}
		});
	}
}

void ChatParticipants::requestLast(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		|| _participantsRequests.contains(channel)) {
		return;
	}

	const auto offset = 0;
	const auto participantsHash = uint64(0);
	const auto requestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(offset),
		MTP_int(_session->serverConfig().chatSizeMax),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_participantsRequests.remove(channel);
		parse(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyLastList(
				channel,
				availableCount,
				list);
		});
	}).fail([this, channel](const MTP::Error &error) {
		_participantsRequests.remove(channel);
	}).send();

	_participantsRequests[channel] = requestId;
}

void ChatParticipants::requestBots(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _botsRequests.contains(channel)) {
		return;
	}

	const auto offset = 0;
	const auto participantsHash = uint64(0);
	const auto requestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsBots(),
		MTP_int(offset),
		MTP_int(_session->serverConfig().chatSizeMax),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_botsRequests.remove(channel);
		parse(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			applyBotsList(
				channel,
				availableCount,
				list);
		});
	}).fail([=](const MTP::Error &error) {
		_botsRequests.remove(channel);
	}).send();

	_botsRequests[channel] = requestId;
}

void ChatParticipants::requestAdmins(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup() || _adminsRequests.contains(channel)) {
		return;
	}

	const auto offset = 0;
	const auto participantsHash = uint64(0);
	const auto requestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsAdmins(),
		MTP_int(offset),
		MTP_int(_session->serverConfig().chatSizeMax),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_adminsRequests.remove(channel);
		result.match([&](const MTPDchannels_channelParticipants &data) {
			ApplyMegagroupAdmins(channel, data);
		}, [&](const MTPDchannels_channelParticipantsNotModified &) {
			LOG(("API Error: channels.channelParticipantsNotModified received!"));
		});
	}).fail([=](const MTP::Error &error) {
		_adminsRequests.remove(channel);
	}).send();

	_adminsRequests[channel] = requestId;
}

void ChatParticipants::requestCountDelayed(
		not_null<ChannelData*> channel) {
	_participantsCountRequestTimer.call(
		kReloadChannelMembersTimeout,
		[=] { channel->updateFullForced(); });
}

void ChatParticipants::add(
		not_null<PeerData*> peer,
		const std::vector<not_null<UserData*>> &users,
		Fn<void(bool)> done) {
	if (const auto chat = peer->asChat()) {
		for (const auto &user : users) {
			_api.request(MTPmessages_AddChatUser(
				chat->inputChat,
				user->inputUser,
				MTP_int(kForwardMessagesOnAdd)
			)).done([=](const MTPUpdates &result) {
				_session->api().applyUpdates(result);
				if (done) done(true);
			}).fail([=](const MTP::Error &error) {
				ShowAddParticipantsError(error.type(), peer, { 1, user });
				if (done) done(false);
			}).afterDelay(kSmallDelayMs).send();
		}
	} else if (const auto channel = peer->asChannel()) {
		const auto hasBot = ranges::any_of(users, &UserData::isBot);
		if (!peer->isMegagroup() && hasBot) {
			ShowAddParticipantsError("USER_BOT", peer, users);
			return;
		}
		auto list = QVector<MTPInputUser>();
		list.reserve(std::min(int(users.size()), int(kMaxUsersPerInvite)));
		const auto send = [&] {
			const auto callback = base::take(done);
			_api.request(MTPchannels_InviteToChannel(
				channel->inputChannel,
				MTP_vector<MTPInputUser>(list)
			)).done([=](const MTPUpdates &result) {
				_session->api().applyUpdates(result);
				requestCountDelayed(channel);
				if (callback) callback(true);
			}).fail([=](const MTP::Error &error) {
				ShowAddParticipantsError(error.type(), peer, users);
				if (callback) callback(false);
			}).afterDelay(kSmallDelayMs).send();
		};
		for (const auto &user : users) {
			list.push_back(user->inputUser);
			if (list.size() == kMaxUsersPerInvite) {
				send();
				list.clear();
			}
		}
		if (!list.empty()) {
			send();
		}
	} else {
		Unexpected("User in ChatParticipants::add.");
	}
}

void ChatParticipants::parseRecent(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified) {
	parse(channel, result, [&](
			int availableCount,
			const QVector<MTPChannelParticipant> &list) {
		auto applyLast = channel->isMegagroup()
			&& (channel->mgInfo->lastParticipants.size() <= list.size());
		if (applyLast) {
			applyLastList(
				channel,
				availableCount,
				list);
		}
		if (callbackList) {
			callbackList(availableCount, list);
		}
	}, std::move(callbackNotModified));
}

void ChatParticipants::parse(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified) {
	result.match([&](const MTPDchannels_channelParticipants &data) {
		_session->data().processUsers(data.vusers());
		if (channel->mgInfo) {
			refreshChannelAdmins(channel, data.vparticipants().v);
		}
		if (callbackList) {
			callbackList(data.vcount().v, data.vparticipants().v);
		}
	}, [&](const MTPDchannels_channelParticipantsNotModified &) {
		if (callbackNotModified) {
			callbackNotModified();
		} else {
			LOG(("API Error: "
				"channels.channelParticipantsNotModified received!"));
		}
	});
}

void ChatParticipants::applyLastList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	channel->mgInfo->lastAdmins.clear();
	channel->mgInfo->lastRestricted.clear();
	channel->mgInfo->lastParticipants.clear();
	channel->mgInfo->lastParticipantsStatus = MegagroupInfo::LastParticipantsUpToDate
		| MegagroupInfo::LastParticipantsOnceReceived;

	auto botStatus = channel->mgInfo->botStatus;
	for (const auto &p : list) {
		const auto participantId = p.match([](
				const MTPDchannelParticipantBanned &data) {
			return peerFromMTP(data.vpeer());
		}, [](const MTPDchannelParticipantLeft &data) {
			return peerFromMTP(data.vpeer());
		}, [](const auto &data) {
			return peerFromUser(data.vuser_id());
		});
		if (!participantId) {
			continue;
		}
		const auto participant = _session->data().peer(participantId);
		const auto user = participant->asUser();
		const auto adminCanEdit = (p.type() == mtpc_channelParticipantAdmin)
			? p.c_channelParticipantAdmin().is_can_edit()
			: (p.type() == mtpc_channelParticipantCreator)
			? channel->amCreator()
			: false;
		const auto adminRights = (p.type() == mtpc_channelParticipantAdmin)
			? ChatAdminRightsInfo(p.c_channelParticipantAdmin().vadmin_rights())
			: (p.type() == mtpc_channelParticipantCreator)
			? ChatAdminRightsInfo(p.c_channelParticipantCreator().vadmin_rights())
			: ChatAdminRightsInfo();
		const auto restrictedRights = (p.type() == mtpc_channelParticipantBanned)
			? ChatRestrictionsInfo(
				p.c_channelParticipantBanned().vbanned_rights())
			: ChatRestrictionsInfo();
		if (p.type() == mtpc_channelParticipantCreator) {
			Assert(user != nullptr);
			const auto &creator = p.c_channelParticipantCreator();
			const auto rank = qs(creator.vrank().value_or_empty());
			channel->mgInfo->creator = user;
			channel->mgInfo->creatorRank = rank;
			if (!channel->mgInfo->admins.empty()) {
				Data::ChannelAdminChanges(channel).add(
					peerToUser(participantId),
					rank);
			}
		}
		if (user
			&& !base::contains(channel->mgInfo->lastParticipants, user)) {
			channel->mgInfo->lastParticipants.push_back(user);
			if (adminRights.flags) {
				channel->mgInfo->lastAdmins.emplace(
					user,
					MegagroupInfo::Admin{ adminRights, adminCanEdit });
			} else if (restrictedRights.flags) {
				channel->mgInfo->lastRestricted.emplace(
					user,
					MegagroupInfo::Restricted{ restrictedRights });
			}
			if (user->isBot()) {
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
	_session->changes().peerUpdated(
		channel,
		(Data::PeerUpdate::Flag::Members | Data::PeerUpdate::Flag::Admins));

	channel->mgInfo->botStatus = botStatus;
	_session->changes().peerUpdated(
		channel,
		Data::PeerUpdate::Flag::FullInfo);
}

void ChatParticipants::applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list) {
	const auto history = _session->data().historyLoaded(channel);
	channel->mgInfo->bots.clear();
	channel->mgInfo->botStatus = -1;

	auto needBotsInfos = false;
	auto botStatus = channel->mgInfo->botStatus;
	auto keyboardBotFound = !history || !history->lastKeyboardFrom;
	for (const auto &p : list) {
		const auto participantId = p.match([](
				const MTPDchannelParticipantBanned &data) {
			return peerFromMTP(data.vpeer());
		}, [](const MTPDchannelParticipantLeft &data) {
			return peerFromMTP(data.vpeer());
		}, [](const auto &data) {
			return peerFromUser(data.vuser_id());
		});
		if (!participantId) {
			continue;
		}

		const auto participant = _session->data().peer(participantId);
		const auto user = participant->asUser();
		if (user && user->isBot()) {
			channel->mgInfo->bots.insert(user);
			botStatus = 2;// (botStatus > 0/* || !i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
			if (!user->botInfo->inited) {
				needBotsInfos = true;
			}
		}
		if (!keyboardBotFound
			&& participant->id == history->lastKeyboardFrom) {
			keyboardBotFound = true;
		}
	}
	if (needBotsInfos) {
		_session->api().requestFullPeer(channel);
	}
	if (!keyboardBotFound) {
		history->clearLastKeyboard();
	}

	channel->mgInfo->botStatus = botStatus;
	_session->changes().peerUpdated(
		channel,
		Data::PeerUpdate::Flag::FullInfo);
}

void ChatParticipants::requestSelf(not_null<ChannelData*> channel) {
	if (_selfParticipantRequests.contains(channel)) {
		return;
	}

	const auto finalize = [=](
			UserId inviter = -1,
			TimeId inviteDate = 0,
			bool inviteViaRequest = false) {
		channel->inviter = inviter;
		channel->inviteDate = inviteDate;
		channel->inviteViaRequest = inviteViaRequest;
		if (const auto history = _session->data().historyLoaded(channel)) {
			if (history->lastMessageKnown()) {
				history->checkLocalMessages();
				history->owner().sendHistoryChangeNotifications();
			} else {
				history->owner().histories().requestDialogEntry(history);
			}
		}
	};
	_selfParticipantRequests.emplace(channel);
	_api.request(MTPchannels_GetParticipant(
		channel->inputChannel,
		MTP_inputPeerSelf()
	)).done([=](const MTPchannels_ChannelParticipant &result) {
		_selfParticipantRequests.erase(channel);
		result.match([&](const MTPDchannels_channelParticipant &data) {
			_session->data().processUsers(data.vusers());

			const auto &participant = data.vparticipant();
			participant.match([&](const MTPDchannelParticipantSelf &data) {
				finalize(
					data.vinviter_id().v,
					data.vdate().v,
					data.is_via_request());
			}, [&](const MTPDchannelParticipantCreator &) {
				if (channel->mgInfo) {
					channel->mgInfo->creator = _session->user();
				}
				finalize(_session->userId(), channel->date);
			}, [&](const MTPDchannelParticipantAdmin &data) {
				const auto inviter = data.is_self()
					? data.vinviter_id().value_or(-1)
					: -1;
				finalize(inviter, data.vdate().v);
			}, [&](const MTPDchannelParticipantBanned &data) {
				LOG(("API Error: Got self banned participant."));
				finalize();
			}, [&](const MTPDchannelParticipant &data) {
				LOG(("API Error: Got self regular participant."));
				finalize();
			}, [&](const MTPDchannelParticipantLeft &data) {
				LOG(("API Error: Got self left participant."));
				finalize();
			});
		});
	}).fail([=](const MTP::Error &error) {
		_selfParticipantRequests.erase(channel);
		if (error.type() == qstr("CHANNEL_PRIVATE")) {
			channel->privateErrorReceived();
		}
		finalize();
	}).afterDelay(kSmallDelayMs).send();
}

void ChatParticipants::kick(
		not_null<ChatData*> chat,
		not_null<PeerData*> participant) {
	Expects(participant->isUser());

	_api.request(MTPmessages_DeleteChatUser(
		MTP_flags(0),
		chat->inputChat,
		participant->asUser()->inputUser
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
	}).send();
}

void ChatParticipants::kick(
		not_null<ChannelData*> channel,
		not_null<PeerData*> participant,
		ChatRestrictionsInfo currentRights) {
	const auto kick = KickRequest(channel, participant);
	if (_kickRequests.contains(kick)) return;

	const auto rights = ChannelData::KickedRestrictedRights(participant);
	const auto requestId = _api.request(MTPchannels_EditBanned(
		channel->inputChannel,
		participant->input,
		MTP_chatBannedRights(
			MTP_flags(
				MTPDchatBannedRights::Flags::from_raw(uint32(rights.flags))),
			MTP_int(rights.until))
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		_kickRequests.remove(KickRequest(channel, participant));
		channel->applyEditBanned(participant, currentRights, rights);
	}).fail([this, kick](const MTP::Error &error) {
		_kickRequests.remove(kick);
	}).send();

	_kickRequests.emplace(kick, requestId);
}

void ChatParticipants::unblock(
		not_null<ChannelData*> channel,
		not_null<PeerData*> participant) {
	const auto kick = KickRequest(channel, participant);
	if (_kickRequests.contains(kick)) {
		return;
	}

	const auto requestId = _api.request(MTPchannels_EditBanned(
		channel->inputChannel,
		participant->input,
		MTP_chatBannedRights(MTP_flags(0), MTP_int(0))
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		_kickRequests.remove(KickRequest(channel, participant));
		if (channel->kickedCount() > 0) {
			channel->setKickedCount(channel->kickedCount() - 1);
		} else {
			channel->updateFullForced();
		}
	}).fail([=](const MTP::Error &error) {
		_kickRequests.remove(kick);
	}).send();

	_kickRequests.emplace(kick, requestId);
}

} // namespace Api
