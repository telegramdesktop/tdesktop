/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_invite_links.h"

#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Api {
namespace {

constexpr auto kFirstPage = 10;
constexpr auto kPerPage = 50;
constexpr auto kJoinedFirstPage = 10;

void BringPermanentToFront(PeerInviteLinks &links) {
	auto &list = links.links;
	const auto i = ranges::find_if(list, [](const InviteLink &link) {
		return link.permanent && !link.revoked;
	});
	if (i != end(list) && i != begin(list)) {
		ranges::rotate(begin(list), i, i + 1);
	}
}

void RemovePermanent(PeerInviteLinks &links) {
	auto &list = links.links;
	list.erase(ranges::remove_if(list, [](const InviteLink &link) {
		return link.permanent && !link.revoked;
	}), end(list));
}

} // namespace

InviteLinks::InviteLinks(not_null<ApiWrap*> api) : _api(api) {
}

void InviteLinks::create(
		not_null<PeerData*> peer,
		Fn<void(Link)> done,
		TimeId expireDate,
		int usageLimit) {
	performCreate(peer, std::move(done), false, expireDate, usageLimit);
}

void InviteLinks::performCreate(
		not_null<PeerData*> peer,
		Fn<void(Link)> done,
		bool revokeLegacyPermanent,
		TimeId expireDate,
		int usageLimit) {
	if (const auto i = _createCallbacks.find(peer)
		; i != end(_createCallbacks)) {
		if (done) {
			i->second.push_back(std::move(done));
		}
		return;
	}
	auto &callbacks = _createCallbacks[peer];
	if (done) {
		callbacks.push_back(std::move(done));
	}

	using Flag = MTPmessages_ExportChatInvite::Flag;
	_api->request(MTPmessages_ExportChatInvite(
		MTP_flags((revokeLegacyPermanent
			? Flag::f_legacy_revoke_permanent
			: Flag(0))
			| (expireDate ? Flag::f_expire_date : Flag(0))
			| (usageLimit ? Flag::f_usage_limit : Flag(0))),
		peer->input,
		MTP_int(expireDate),
		MTP_int(usageLimit)
	)).done([=](const MTPExportedChatInvite &result) {
		const auto callbacks = _createCallbacks.take(peer);
		const auto link = prepend(peer, result);
		if (callbacks) {
			for (const auto &callback : *callbacks) {
				callback(link);
			}
		}
	}).fail([=](const RPCError &error) {
		_createCallbacks.erase(peer);
	}).send();
}

auto InviteLinks::lookupPermanent(not_null<PeerData*> peer) -> Link* {
	auto i = _firstSlices.find(peer);
	return (i != end(_firstSlices)) ? lookupPermanent(i->second) : nullptr;
}

auto InviteLinks::lookupPermanent(Links &links) -> Link* {
	const auto first = links.links.begin();
	return (first != end(links.links) && first->permanent && !first->revoked)
		? &*first
		: nullptr;
}

auto InviteLinks::lookupPermanent(const Links &links) const -> const Link* {
	const auto first = links.links.begin();
	return (first != end(links.links) && first->permanent && !first->revoked)
		? &*first
		: nullptr;
}

auto InviteLinks::prepend(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) -> Link {
	const auto link = parse(peer, invite);
	auto i = _firstSlices.find(peer);
	if (i == end(_firstSlices)) {
		i = _firstSlices.emplace(peer).first;
	}
	auto &links = i->second;
	const auto permanent = lookupPermanent(links);
	if (link.permanent) {
		if (permanent) {
			permanent->revoked = true;
		}
		editPermanentLink(peer, link.link);
	}
	++links.count;
	if (permanent && !link.permanent) {
		links.links.insert(begin(links.links) + 1, link);
	} else {
		links.links.insert(begin(links.links), link);
	}
	notify(peer);
	return link;
}

void InviteLinks::edit(
		not_null<PeerData*> peer,
		const QString &link,
		TimeId expireDate,
		int usageLimit,
		Fn<void(Link)> done) {
	performEdit(peer, link, std::move(done), false, expireDate, usageLimit);
}

void InviteLinks::performEdit(
		not_null<PeerData*> peer,
		const QString &link,
		Fn<void(Link)> done,
		bool revoke,
		TimeId expireDate,
		int usageLimit) {
	const auto key = LinkKey{ peer, link };
	if (const auto i = _editCallbacks.find(key); i != end(_editCallbacks)) {
		if (done) {
			i->second.push_back(std::move(done));
		}
		return;
	}

	if (const auto permanent = revoke ? lookupPermanent(peer) : nullptr) {
		if (permanent->link == link) {
			// In case of revoking a permanent link
			// we should just create a new one instead.
			performCreate(peer, std::move(done), true);
			return;
		}
	}

	auto &callbacks = _editCallbacks[key];
	if (done) {
		callbacks.push_back(std::move(done));
	}

	using Flag = MTPmessages_EditExportedChatInvite::Flag;
	const auto requestId = _api->request(MTPmessages_EditExportedChatInvite(
		MTP_flags((revoke ? Flag::f_revoked : Flag(0))
			| ((!revoke && expireDate) ? Flag::f_expire_date : Flag(0))
			| ((!revoke && usageLimit) ? Flag::f_usage_limit : Flag(0))),
		peer->input,
		MTP_string(link),
		MTP_int(expireDate),
		MTP_int(usageLimit)
	)).done([=](const MTPmessages_ExportedChatInvite &result) {
		const auto callbacks = _editCallbacks.take(key);
		const auto peer = key.peer;
		result.match([&](const MTPDmessages_exportedChatInvite &data) {
			_api->session().data().processUsers(data.vusers());
			const auto link = parse(peer, data.vinvite());
			auto i = _firstSlices.find(peer);
			if (i != end(_firstSlices)) {
				const auto j = ranges::find(
					i->second.links,
					key.link,
					&Link::link);
				if (j != end(i->second.links)) {
					*j = link;
					notify(peer);
				}
			}
			for (const auto &callback : *callbacks) {
				callback(link);
			}
		});
	}).fail([=](const RPCError &error) {
		_editCallbacks.erase(key);
	}).send();
}

void InviteLinks::revoke(
		not_null<PeerData*> peer,
		const QString &link,
		Fn<void(Link)> done) {
	performEdit(peer, link, std::move(done), true);
}

void InviteLinks::revokePermanent(
		not_null<PeerData*> peer,
		Fn<void(Link)> done) {
	performCreate(peer, std::move(done), true);
}

void InviteLinks::requestLinks(not_null<PeerData*> peer) {
	if (_firstSliceRequests.contains(peer)) {
		return;
	}
	const auto requestId = _api->request(MTPmessages_GetExportedChatInvites(
		MTP_flags(0),
		peer->input,
		MTPInputUser(), // admin_id
		MTPstring(), // offset_link
		MTP_int(kFirstPage)
	)).done([=](const MTPmessages_ExportedChatInvites &result) {
		_firstSliceRequests.remove(peer);
		auto slice = parseSlice(peer, result);
		auto i = _firstSlices.find(peer);
		const auto permanent = (i != end(_firstSlices))
			? lookupPermanent(i->second)
			: nullptr;
		if (!permanent) {
			BringPermanentToFront(slice);
			const auto j = _firstSlices.emplace_or_assign(
				peer,
				std::move(slice)).first;
			if (const auto permanent = lookupPermanent(j->second)) {
				editPermanentLink(peer, permanent->link);
			}
		} else {
			RemovePermanent(slice);
			auto &existing = i->second.links;
			existing.erase(begin(existing) + 1, end(existing));
			existing.insert(
				end(existing),
				begin(slice.links),
				end(slice.links));
			i->second.count = std::max(slice.count, int(existing.size()));
		}
		notify(peer);
	}).fail([=](const RPCError &error) {
		_firstSliceRequests.remove(peer);
	}).send();
	_firstSliceRequests.emplace(peer, requestId);
}

JoinedByLinkSlice InviteLinks::lookupJoinedFirstSlice(LinkKey key) const {
	const auto i = _firstJoined.find(key);
	return (i != end(_firstJoined)) ? i->second : JoinedByLinkSlice();
}

rpl::producer<JoinedByLinkSlice> InviteLinks::joinedFirstSliceValue(
		not_null<PeerData*> peer,
		const QString &link,
		int fullCount) {
	const auto key = LinkKey{ peer, link };
	auto current = lookupJoinedFirstSlice(key);
	if (current.count == fullCount
		&& (!fullCount || !current.users.empty())) {
		return rpl::single(current);
	}
	current.count = fullCount;
	const auto remove = int(current.users.size()) - current.count;
	if (remove > 0) {
		current.users.erase(end(current.users) - remove, end(current.users));
	}
	requestJoinedFirstSlice(key);
	using namespace rpl::mappers;
	return rpl::single(
		current
	) | rpl::then(_joinedFirstSliceLoaded.events(
	) | rpl::filter(
		_1 == key
	) | rpl::map([=] {
		return lookupJoinedFirstSlice(key);
	}));
}

void InviteLinks::requestJoinedFirstSlice(LinkKey key) {
	if (_firstJoinedRequests.contains(key)) {
		return;
	}
	const auto requestId = _api->request(MTPmessages_GetChatInviteImporters(
		key.peer->input,
		MTP_string(key.link),
		MTP_int(0), // offset_date
		MTP_inputUserEmpty(), // offset_user
		MTP_int(kJoinedFirstPage)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		_firstJoinedRequests.remove(key);
		_firstJoined[key] = parseSlice(key.peer, result);
		_joinedFirstSliceLoaded.fire_copy(key);
	}).fail([=](const RPCError &error) {
		_firstJoinedRequests.remove(key);
	}).send();
	_firstJoinedRequests.emplace(key, requestId);
}

void InviteLinks::setPermanent(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) {
	auto link = parse(peer, invite);
	if (!link.permanent) {
		LOG(("API Error: "
			"InviteLinks::setPermanent called with non-permanent link."));
		return;
	}
	auto i = _firstSlices.find(peer);
	if (i == end(_firstSlices)) {
		i = _firstSlices.emplace(peer).first;
	}
	auto &links = i->second;
	if (const auto permanent = lookupPermanent(links)) {
		if (permanent->link == link.link) {
			if (permanent->usage != link.usage) {
				permanent->usage = link.usage;
				notify(peer);
			}
			return;
		}
		permanent->revoked = true;
	}
	links.links.insert(begin(links.links), link);
	editPermanentLink(peer, link.link);
	notify(peer);
}

void InviteLinks::clearPermanent(not_null<PeerData*> peer) {
	if (const auto permanent = lookupPermanent(peer)) {
		permanent->revoked = true;
		editPermanentLink(peer, QString());
		notify(peer);
	}
}

void InviteLinks::notify(not_null<PeerData*> peer) {
	peer->session().changes().peerUpdated(
		peer,
		Data::PeerUpdate::Flag::InviteLinks);
}

auto InviteLinks::links(not_null<PeerData*> peer) const -> const Links & {
	static const auto kEmpty = Links();
	const auto i = _firstSlices.find(peer);
	return (i != end(_firstSlices)) ? i->second : kEmpty;
}

auto InviteLinks::parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const -> Links {
	auto i = _firstSlices.find(peer);
	const auto permanent = (i != end(_firstSlices))
		? lookupPermanent(i->second)
		: nullptr;
	auto result = Links();
	slice.match([&](const MTPDmessages_exportedChatInvites &data) {
		peer->session().data().processUsers(data.vusers());
		result.count = data.vcount().v;
		for (const auto &invite : data.vinvites().v) {
			const auto link = parse(peer, invite);
			if (!permanent || link.link != permanent->link) {
				result.links.push_back(link);
			}
		}
	});
	return result;
}

JoinedByLinkSlice InviteLinks::parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ChatInviteImporters &slice) const {
	auto result = JoinedByLinkSlice();
	slice.match([&](const MTPDmessages_chatInviteImporters &data) {
		auto &owner = peer->session().data();
		owner.processUsers(data.vusers());
		result.count = data.vcount().v;
		result.users.reserve(data.vimporters().v.size());
		for (const auto importer : data.vimporters().v) {
			importer.match([&](const MTPDchatInviteImporter &data) {
				result.users.push_back({
					.user = owner.user(data.vuser_id().v),
					.date = data.vdate().v,
				});
			});
		}
	});
	return result;
}

auto InviteLinks::parse(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) const -> Link {
	return invite.match([&](const MTPDchatInviteExported &data) {
		return Link{
			.link = qs(data.vlink()),
			.admin = peer->session().data().user(data.vadmin_id().v),
			.date = data.vdate().v,
			.startDate = data.vstart_date().value_or_empty(),
			.expireDate = data.vexpire_date().value_or_empty(),
			.usageLimit = data.vusage_limit().value_or_empty(),
			.usage = data.vusage().value_or_empty(),
			.permanent = data.is_permanent(),
			.expired = data.is_expired(),
			.revoked = data.is_revoked(),
		};
	});
}

void InviteLinks::requestMoreLinks(
		not_null<PeerData*> peer,
		const QString &last,
		Fn<void(Links)> done) {
	_api->request(MTPmessages_GetExportedChatInvites(
		MTP_flags(MTPmessages_GetExportedChatInvites::Flag::f_offset_link),
		peer->input,
		MTPInputUser(), // admin_id,
		MTP_string(last),
		MTP_int(kPerPage)
	)).done([=](const MTPmessages_ExportedChatInvites &result) {
		auto slice = parseSlice(peer, result);
		RemovePermanent(slice);
		done(std::move(slice));
	}).fail([=](const RPCError &error) {
		done(Links());
	}).send();
}

void InviteLinks::editPermanentLink(
		not_null<PeerData*> peer,
		const QString &link) {
	if (const auto chat = peer->asChat()) {
		chat->setInviteLink(link);
	} else if (const auto channel = peer->asChannel()) {
		channel->setInviteLink(link);
	} else {
		Unexpected("Peer in InviteLinks::editMainLink.");
	}
}

} // namespace Api
