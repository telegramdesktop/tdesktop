/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_communities.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] ChannelData *ExtractCreatedCommunity(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	const auto chats = updates.match([](const MTPDupdates &data) {
		return &data.vchats().v;
	}, [](const MTPDupdatesCombined &data) {
		return &data.vchats().v;
	}, [](const auto &) -> const QVector<MTPChat>* {
		return nullptr;
	});
	if (!chats) {
		LOG(("API Error: unexpected update cons %1 "
			"(Communities::create)").arg(updates.type()));
		return nullptr;
	}
	for (const auto &chat : *chats) {
		if (chat.type() == mtpc_community) {
			const auto channel = session->data().channelLoaded(
				chat.c_community().vid().v);
			if (channel && channel->isCommunity()) {
				return channel;
			}
		}
	}
	return nullptr;
}

} // namespace

int CommunityPeersLimit(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"community_peers_limit"_q,
		session->isTestMode() ? 10 : 100);
}

int CommunityBotPeersLimit(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"community_bot_peers_limit"_q,
		session->isTestMode() ? 10 : 100);
}

QString CommunityPeersLimitToast(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	return peer->isUser()
		? tr::lng_community_bot_peers_limit(
			tr::now,
			lt_count,
			CommunityBotPeersLimit(session))
		: tr::lng_community_peers_limit(
			tr::now,
			lt_count,
			CommunityPeersLimit(session));
}

Communities::Communities(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void Communities::create(
		const QString &title,
		const QString &about,
		not_null<PeerData*> peer,
		bool hidden,
		Fn<void(not_null<ChannelData*>)> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPcommunities_Create::Flag;
	const auto flags = (about.isEmpty() ? Flag() : Flag::f_about)
		| (hidden ? Flag::f_hidden : Flag());
	_api.request(MTPcommunities_Create(
		MTP_flags(flags),
		MTP_string(title),
		MTP_string(about),
		peer->input()
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
		if (const auto community = ExtractCreatedCommunity(
				_session,
				result)) {
			_session->api().requestFullPeer(community);
			if (done) {
				done(community);
			}
		} else if (fail) {
			fail(QString());
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void Communities::addPeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		bool visible,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	togglePeerLink(
		community,
		peer,
		visible ? PeerLinkAction::Visible : PeerLinkAction::Hidden,
		std::move(done),
		std::move(fail));
}

void Communities::removePeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	togglePeerLink(
		community,
		peer,
		PeerLinkAction::Deleted,
		std::move(done),
		std::move(fail));
}

void Communities::togglePeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		PeerLinkAction action,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPcommunities_TogglePeerLink::Flag;
	const auto flags = (action == PeerLinkAction::Deleted)
		? Flag::f_deleted
		: (action == PeerLinkAction::Hidden)
		? Flag::f_hidden
		: Flag::f_visible;
	_api.request(MTPcommunities_TogglePeerLink(
		MTP_flags(flags),
		community->inputChannel(),
		peer->input()
	)).done([=] {
		_session->api().requestFullPeer(community);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == kCommunityRequestCreated.utf16()) {
			_session->api().requestFullPeer(community);
		}
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void Communities::requestJoinedCommunities(
		Fn<void(const std::vector<not_null<ChannelData*>> &)> done) {
	if (_joinedRequestId) {
		_api.request(_joinedRequestId).cancel();
	}
	_joinedRequestId = _api.request(MTPcommunities_GetJoinedCommunities(
	)).done([=](const MTPmessages_Chats &result) {
		_joinedRequestId = 0;
		auto list = std::vector<not_null<ChannelData*>>();
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		list.reserve(chats.size());
		for (const auto &chat : chats) {
			const auto peer = _session->data().processChat(chat);
			if (const auto channel = peer->asChannel()) {
				if (channel->isCommunity() && !channel->isForbidden()) {
					list.push_back(channel);
				}
			}
		}
		if (done) {
			done(list);
		}
	}).fail([=] {
		_joinedRequestId = 0;
		if (done) {
			done({});
		}
	}).send();
}

void Communities::toggleCollapsedInDialogs(
		not_null<ChannelData*> community,
		bool collapsed) {
	if (!_collapseRequests.emplace(community).second) {
		return;
	}
	const auto history = community->owner().history(community);
	const auto was = (community->flags()
		& ChannelDataFlag::CommunityCollapsed) != 0;
	const auto wasPinned = history->folderKnown()
		&& history->isPinnedDialog(FilterId());
	const auto apply = [=](bool value) {
		if (value) {
			community->addFlags(ChannelDataFlag::CommunityCollapsed);
		} else {
			community->removeFlags(ChannelDataFlag::CommunityCollapsed);
		}
	};

	// Clearing the flag drops the grouped row from the chat list, which
	// self-unpins it; the server's updateDialogPinned confirms it on done.
	apply(collapsed);
	using Flag = MTPcommunities_ToggleCommunityCollapsedInDialogs::Flag;
	_api.request(MTPcommunities_ToggleCommunityCollapsedInDialogs(
		MTP_flags(collapsed ? Flag::f_collapsed : Flag()),
		community->inputChannel()
	)).done([=](const MTPUpdates &result) {
		_collapseRequests.remove(community);
		_session->api().applyUpdates(result);
	}).fail([=] {
		_collapseRequests.remove(community);
		apply(was);
		if (wasPinned) {
			community->owner().setChatPinned(history, FilterId(), true);
		}
	}).send();
}

void Communities::requestPeerLinkRequests(
		not_null<ChannelData*> community,
		const QString &offset,
		int limit,
		Fn<void(CommunityPeerRequestsSlice)> done) {
	const auto i = _peerLinkRequestsRequests.find(community);
	if (i != end(_peerLinkRequestsRequests)) {
		_api.request(i->second).cancel();
		_peerLinkRequestsRequests.erase(i);
	}
	const auto requestId = _api.request(MTPcommunities_GetPeerLinkRequests(
		community->inputChannel(),
		MTP_string(offset),
		MTP_int(limit)
	)).done([=](const MTPcommunities_PeerLinkRequests &result) {
		_peerLinkRequestsRequests.remove(community);
		const auto &data = result.data();
		auto &owner = _session->data();
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		community->setPendingRequestsCount(
			data.vtotal_count().v,
			QVector<MTPlong>());
		auto slice = CommunityPeerRequestsSlice();
		slice.totalCount = data.vtotal_count().v;
		slice.nextOffset = qs(data.vnext_offset().value_or_empty());
		slice.list.reserve(data.vrequests().v.size());
		for (const auto &request : data.vrequests().v) {
			const auto &fields = request.data();
			slice.list.push_back({
				.peer = owner.peer(peerFromMTP(fields.vpeer())),
				.requestedBy = owner.userLoaded(
					UserId(fields.vrequested_by())),
				.date = fields.vdate().v,
				.visible = fields.is_visible(),
			});
		}
		if (done) {
			done(std::move(slice));
		}
	}).fail([=] {
		_peerLinkRequestsRequests.remove(community);
		if (done) {
			done({});
		}
	}).send();
	_peerLinkRequestsRequests[community] = requestId;
}

void Communities::togglePeerLinkRequestApproval(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		bool reject,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPcommunities_TogglePeerLinkRequestApproval::Flag;
	_api.request(MTPcommunities_TogglePeerLinkRequestApproval(
		MTP_flags(reject ? Flag::f_reject : Flag()),
		community->inputChannel(),
		peer->input()
	)).done([=] {
		// Optimistic decrement for instant feedback, then a forced fresh full
		// fetch for the authoritative count and the newly-added member. A plain
		// requestFullPeer() would be deduped against an in-flight full fetch (the
		// surface requests it lazily) and the older stale response would clobber
		// the count back to the pre-approval value; reloadFullPeer cancels that
		// stale request and refetches.
		community->setPendingRequestsCount(
			std::max(community->pendingRequestsCount() - 1, 0),
			QVector<MTPlong>());
		_session->api().reloadFullPeer(community);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void Communities::toggleAllPeerLinkRequestApproval(
		not_null<ChannelData*> community,
		bool reject,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPcommunities_ToggleAllPeerLinkRequestApproval::Flag;
	_api.request(MTPcommunities_ToggleAllPeerLinkRequestApproval(
		MTP_flags(reject ? Flag::f_reject : Flag()),
		community->inputChannel()
	)).done([=] {
		// Optimistic reset, then a forced fresh full fetch for the authoritative
		// state; see togglePeerLinkRequestApproval for why reloadFullPeer (not a
		// dedupable requestFullPeer) is used here.
		community->setPendingRequestsCount(0, QVector<MTPlong>());
		_session->api().reloadFullPeer(community);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void Communities::toggleParticipantBanned(
		not_null<ChannelData*> community,
		not_null<PeerData*> participant,
		bool unban,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPcommunities_ToggleParticipantBanned::Flag;
	_api.request(MTPcommunities_ToggleParticipantBanned(
		MTP_flags(unban ? Flag::f_unban : Flag()),
		community->inputChannel(),
		participant->input()
	)).done([=] {
		_session->api().requestFullPeer(community);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

void Communities::requestParticipantJoinedChats(
		not_null<ChannelData*> community,
		not_null<PeerData*> participant,
		Fn<void(CommunityParticipantJoinedChats)> done) {
	_api.request(MTPcommunities_GetParticipantJoinedChats(
		community->inputChannel(),
		participant->input()
	)).done([=](const MTPcommunities_ParticipantJoinedChats &result) {
		const auto &data = result.data();
		auto &owner = _session->data();
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		auto parsed = CommunityParticipantJoinedChats();
		const auto append = [&](
				std::vector<not_null<PeerData*>> &to,
				const QVector<MTPlong> &ids) {
			to.reserve(ids.size());
			for (const auto &id : ids) {
				if (const auto chat = owner.channelLoaded(id.v)) {
					to.push_back(chat);
				}
			}
		};
		append(parsed.creatorChats, data.vcreator_chat_ids().v);
		append(parsed.joinedChats, data.vjoined_chat_ids().v);
		if (done) {
			done(std::move(parsed));
		}
	}).fail([=] {
		if (done) {
			done({});
		}
	}).send();
}

} // namespace Api
