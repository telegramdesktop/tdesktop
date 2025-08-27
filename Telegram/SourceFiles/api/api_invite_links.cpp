/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_invite_links.h"

#include "api/api_chat_participants.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "base/unixtime.h"
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

JoinedByLinkSlice ParseJoinedByLinkSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ChatInviteImporters &slice) {
	auto result = JoinedByLinkSlice();
	slice.match([&](const MTPDmessages_chatInviteImporters &data) {
		auto &owner = peer->session().data();
		owner.processUsers(data.vusers());
		result.count = data.vcount().v;
		result.users.reserve(data.vimporters().v.size());
		for (const auto &importer : data.vimporters().v) {
			importer.match([&](const MTPDchatInviteImporter &data) {
				result.users.push_back({
					.user = owner.user(data.vuser_id()),
					.date = data.vdate().v,
					.viaFilterLink = data.is_via_chatlist(),
				});
			});
		}
	});
	return result;
}

InviteLinks::InviteLinks(not_null<ApiWrap*> api) : _api(api) {
}

void InviteLinks::create(const CreateInviteLinkArgs &args) {
	performCreate(args, false);
}

void InviteLinks::performCreate(
		const CreateInviteLinkArgs &args,
		bool revokeLegacyPermanent) {
	if (const auto i = _createCallbacks.find(args.peer)
		; i != end(_createCallbacks)) {
		if (args.done) {
			i->second.push_back(std::move(args.done));
		}
		return;
	}
	auto &callbacks = _createCallbacks[args.peer];
	if (args.done) {
		callbacks.push_back(std::move(args.done));
	}

	const auto requestApproval = !args.subscription && args.requestApproval;
	using Flag = MTPmessages_ExportChatInvite::Flag;
	_api->request(MTPmessages_ExportChatInvite(
		MTP_flags((revokeLegacyPermanent
			? Flag::f_legacy_revoke_permanent
			: Flag(0))
			| (!args.label.isEmpty() ? Flag::f_title : Flag(0))
			| (args.expireDate ? Flag::f_expire_date : Flag(0))
			| ((!requestApproval && args.usageLimit)
				? Flag::f_usage_limit
				: Flag(0))
			| (requestApproval ? Flag::f_request_needed : Flag(0))
			| (args.subscription ? Flag::f_subscription_pricing : Flag(0))),
		args.peer->input,
		MTP_int(args.expireDate),
		MTP_int(args.usageLimit),
		MTP_string(args.label),
		MTP_starsSubscriptionPricing(
			MTP_int(args.subscription.period),
			MTP_long(args.subscription.credits))
	)).done([=, peer = args.peer](const MTPExportedChatInvite &result) {
		const auto callbacks = _createCallbacks.take(peer);
		const auto link = prepend(peer, peer->session().user(), result);
		if (link && callbacks) {
			for (const auto &callback : *callbacks) {
				callback(*link);
			}
		}
	}).fail([=, peer = args.peer] {
		_createCallbacks.erase(peer);
	}).send();
}

auto InviteLinks::lookupMyPermanent(not_null<PeerData*> peer) -> Link* {
	auto i = _firstSlices.find(peer);
	return (i != end(_firstSlices)) ? lookupMyPermanent(i->second) : nullptr;
}

auto InviteLinks::lookupMyPermanent(Links &links) -> Link* {
	const auto first = links.links.begin();
	return (first != end(links.links) && first->permanent && !first->revoked)
		? &*first
		: nullptr;
}

auto InviteLinks::lookupMyPermanent(const Links &links) const -> const Link* {
	const auto first = links.links.begin();
	return (first != end(links.links) && first->permanent && !first->revoked)
		? &*first
		: nullptr;
}

auto InviteLinks::prepend(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const MTPExportedChatInvite &invite) -> std::optional<Link> {
	const auto link = parse(peer, invite);
	if (!link) {
		return link;
	}
	if (admin->isSelf()) {
		prependMyToFirstSlice(peer, admin, *link);
	}
	_updates.fire(Update{
		.peer = peer,
		.admin = admin,
		.now = *link
	});
	return link;
}

void InviteLinks::prependMyToFirstSlice(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const Link &link) {
	Expects(admin->isSelf());

	auto i = _firstSlices.find(peer);
	if (i == end(_firstSlices)) {
		i = _firstSlices.emplace(peer).first;
	}
	auto &links = i->second;
	const auto permanent = lookupMyPermanent(links);
	const auto hadPermanent = (permanent != nullptr);
	auto updateOldPermanent = Update{
		.peer = peer,
		.admin = admin,
	};
	if (link.permanent && hadPermanent) {
		updateOldPermanent.was = permanent->link;
		updateOldPermanent.now = *permanent;
		updateOldPermanent.now->revoked = true;
		links.links.erase(begin(links.links));
		if (links.count > 0) {
			--links.count;
		}
	}
	// Must not dereference 'permanent' pointer after that.

	++links.count;
	if (hadPermanent && !link.permanent) {
		links.links.insert(begin(links.links) + 1, link);
	} else {
		links.links.insert(begin(links.links), link);
	}

	if (link.permanent) {
		editPermanentLink(peer, link.link);
	}
	notify(peer);

	if (updateOldPermanent.now) {
		_updates.fire(std::move(updateOldPermanent));
	}
}

void InviteLinks::edit(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		const QString &label,
		TimeId expireDate,
		int usageLimit,
		bool requestApproval,
		Fn<void(Link)> done) {
	performEdit(
		peer,
		admin,
		link,
		std::move(done),
		false,
		label,
		expireDate,
		usageLimit,
		requestApproval);
}

void InviteLinks::editTitle(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		const QString &label,
		Fn<void(Link)> done) {
	performEdit(peer, admin, link, done, false, label, 0, 0, false, true);
}

void InviteLinks::performEdit(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void(Link)> done,
		bool revoke,
		const QString &label,
		TimeId expireDate,
		int usageLimit,
		bool requestApproval,
		bool editOnlyTitle) {
	const auto key = LinkKey{ peer, link };
	if (_deleteCallbacks.contains(key)) {
		return;
	} else if (const auto i = _editCallbacks.find(key)
		; i != end(_editCallbacks)) {
		if (done) {
			i->second.push_back(std::move(done));
		}
		return;
	}

	auto &callbacks = _editCallbacks[key];
	if (done) {
		callbacks.push_back(std::move(done));
	}
	using Flag = MTPmessages_EditExportedChatInvite::Flag;
	const auto flags = (revoke ? Flag::f_revoked : Flag(0))
		| (!revoke ? Flag::f_title : Flag(0))
		| (!revoke ? Flag::f_expire_date : Flag(0))
		| ((!revoke && !requestApproval) ? Flag::f_usage_limit : Flag(0))
		| ((!revoke && (requestApproval || !usageLimit))
			? Flag::f_request_needed
			: Flag(0));
	_api->request(MTPmessages_EditExportedChatInvite(
		MTP_flags(editOnlyTitle ? Flag::f_title : flags),
		peer->input,
		MTP_string(link),
		MTP_int(expireDate),
		MTP_int(usageLimit),
		MTP_bool(requestApproval),
		MTP_string(label)
	)).done([=](const MTPmessages_ExportedChatInvite &result) {
		const auto callbacks = _editCallbacks.take(key);
		const auto peer = key.peer;
		result.match([&](const auto &data) {
			_api->session().data().processUsers(data.vusers());
			const auto link = parse(peer, data.vinvite());
			if (!link) {
				return;
			}
			auto i = _firstSlices.find(peer);
			if (i != end(_firstSlices)) {
				const auto j = ranges::find(
					i->second.links,
					key.link,
					&Link::link);
				if (j != end(i->second.links)) {
					if (link->revoked && !j->revoked) {
						i->second.links.erase(j);
						if (i->second.count > 0) {
							--i->second.count;
						}
					} else {
						*j = *link;
					}
				}
			}
			for (const auto &callback : *callbacks) {
				callback(*link);
			}
			_updates.fire(Update{
				.peer = peer,
				.admin = admin,
				.was = key.link,
				.now = link,
			});

			using Replaced = MTPDmessages_exportedChatInviteReplaced;
			if constexpr (Replaced::Is<decltype(data)>()) {
				prepend(peer, admin, data.vnew_invite());
			}
		});
	}).fail([=] {
		_editCallbacks.erase(key);
	}).send();
}

void InviteLinks::revoke(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void(Link)> done) {
	performEdit(peer, admin, link, std::move(done), true);
}

void InviteLinks::revokePermanent(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void()> done) {
	const auto callback = [=](auto&&) { done(); };
	if (!link.isEmpty()) {
		performEdit(peer, admin, link, callback, true);
	} else if (!admin->isSelf()) {
		crl::on_main(&peer->session(), done);
	} else {
		performCreate({ peer, callback }, true);
	}
}

void InviteLinks::destroy(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void()> done) {
	const auto key = LinkKey{ peer, link };

	if (const auto i = _deleteCallbacks.find(key)
		; i != end(_deleteCallbacks)) {
		if (done) {
			i->second.push_back(std::move(done));
		}
		return;
	}

	auto &callbacks = _deleteCallbacks[key];
	if (done) {
		callbacks.push_back(std::move(done));
	}
	_api->request(MTPmessages_DeleteExportedChatInvite(
		peer->input,
		MTP_string(link)
	)).done([=] {
		const auto callbacks = _deleteCallbacks.take(key);
		if (callbacks) {
			for (const auto &callback : *callbacks) {
				callback();
			}
		}
		_updates.fire(Update{
			.peer = peer,
			.admin = admin,
			.was = key.link,
		});
	}).fail([=] {
		_deleteCallbacks.erase(key);
	}).send();
}

void InviteLinks::destroyAllRevoked(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		Fn<void()> done) {
	if (const auto i = _deleteRevokedCallbacks.find(peer)
		; i != end(_deleteRevokedCallbacks)) {
		if (done) {
			i->second.push_back(std::move(done));
		}
		return;
	}
	auto &callbacks = _deleteRevokedCallbacks[peer];
	if (done) {
		callbacks.push_back(std::move(done));
	}
	_api->request(MTPmessages_DeleteRevokedExportedChatInvites(
		peer->input,
		admin->inputUser
	)).done([=] {
		if (const auto callbacks = _deleteRevokedCallbacks.take(peer)) {
			for (const auto &callback : *callbacks) {
				callback();
			}
		}
		_allRevokedDestroyed.fire({ peer, admin });
	}).send();
}

void InviteLinks::requestMyLinks(not_null<PeerData*> peer) {
	if (_firstSliceRequests.contains(peer)) {
		return;
	}
	const auto requestId = _api->request(MTPmessages_GetExportedChatInvites(
		MTP_flags(0),
		peer->input,
		MTP_inputUserSelf(),
		MTPint(), // offset_date
		MTPstring(), // offset_link
		MTP_int(kFirstPage)
	)).done([=](const MTPmessages_ExportedChatInvites &result) {
		_firstSliceRequests.remove(peer);
		auto slice = parseSlice(peer, result);
		auto i = _firstSlices.find(peer);
		const auto permanent = (i != end(_firstSlices))
			? lookupMyPermanent(i->second)
			: nullptr;
		if (!permanent) {
			BringPermanentToFront(slice);
			const auto j = _firstSlices.emplace_or_assign(
				peer,
				std::move(slice)).first;
			if (const auto permanent = lookupMyPermanent(j->second)) {
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
	}).fail([=] {
		_firstSliceRequests.remove(peer);
	}).send();
	_firstSliceRequests.emplace(peer, requestId);
}

void InviteLinks::processRequest(
		not_null<PeerData*> peer,
		const QString &link,
		not_null<UserData*> user,
		bool approved,
		Fn<void()> done,
		Fn<void()> fail) {
	if (_processRequests.contains({ peer, user })) {
		return;
	}
	_processRequests.emplace(
		std::pair{ peer, user },
		ProcessRequest{ std::move(done), std::move(fail) });
	using Flag = MTPmessages_HideChatJoinRequest::Flag;
	_api->request(MTPmessages_HideChatJoinRequest(
		MTP_flags(approved ? Flag::f_approved : Flag(0)),
		peer->input,
		user->inputUser
	)).done([=](const MTPUpdates &result) {
		if (const auto chat = peer->asChat()) {
			if (chat->count > 0) {
				if (chat->participants.size() >= chat->count) {
					chat->participants.emplace(user);
				}
				++chat->count;
			}
		} else if (const auto channel = peer->asChannel()) {
			_api->chatParticipants().requestCountDelayed(channel);
		}
		_api->applyUpdates(result);
		if (link.isEmpty() && approved) {
			// We don't know the link that was used for this user.
			// Prune all the cache.
			for (auto i = begin(_firstJoined); i != end(_firstJoined);) {
				if (i->first.peer == peer) {
					i = _firstJoined.erase(i);
				} else {
					++i;
				}
			}
			_firstSlices.remove(peer);
		} else if (approved) {
			const auto i = _firstJoined.find({ peer, link });
			if (i != end(_firstJoined)) {
				++i->second.count;
				i->second.users.insert(
					begin(i->second.users),
					JoinedByLinkUser{ user, base::unixtime::now() });
			}
		}
		if (const auto callbacks = _processRequests.take({ peer, user })) {
			if (const auto &done = callbacks->done) {
				done();
			}
		}
	}).fail([=] {
		if (const auto callbacks = _processRequests.take({ peer, user })) {
			if (const auto &fail = callbacks->fail) {
				fail();
			}
		}
	}).send();
}

void InviteLinks::applyExternalUpdate(
		not_null<PeerData*> peer,
		InviteLink updated) {
	if (const auto i = _firstSlices.find(peer); i != end(_firstSlices)) {
		for (auto &link : i->second.links) {
			if (link.link == updated.link) {
				link = updated;
			}
		}
	}
	_updates.fire({
		.peer = peer,
		.admin = updated.admin,
		.was = updated.link,
		.now = updated,
	});
}

std::optional<JoinedByLinkSlice> InviteLinks::lookupJoinedFirstSlice(
		LinkKey key) const {
	const auto i = _firstJoined.find(key);
	return (i != end(_firstJoined))
		? std::make_optional(i->second)
		: std::nullopt;
}

std::optional<JoinedByLinkSlice> InviteLinks::joinedFirstSliceLoaded(
		not_null<PeerData*> peer,
		const QString &link) const {
	return lookupJoinedFirstSlice({ peer, link });
}

rpl::producer<JoinedByLinkSlice> InviteLinks::joinedFirstSliceValue(
		not_null<PeerData*> peer,
		const QString &link,
		int fullCount) {
	const auto key = LinkKey{ peer, link };
	auto current = lookupJoinedFirstSlice(key).value_or(JoinedByLinkSlice());
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
		return lookupJoinedFirstSlice(key).value_or(JoinedByLinkSlice());
	}));
}

auto InviteLinks::updates(
		not_null<PeerData*> peer,
		not_null<UserData*> admin) const -> rpl::producer<Update> {
	return _updates.events() | rpl::filter([=](const Update &update) {
		return update.peer == peer && update.admin == admin;
	});
}

rpl::producer<> InviteLinks::allRevokedDestroyed(
		not_null<PeerData*> peer,
		not_null<UserData*> admin) const {
	return _allRevokedDestroyed.events(
	) | rpl::filter([=](const AllRevokedDestroyed &which) {
		return which.peer == peer && which.admin == admin;
	}) | rpl::to_empty;
}

void InviteLinks::requestJoinedFirstSlice(LinkKey key) {
	if (_firstJoinedRequests.contains(key)) {
		return;
	}
	const auto requestId = _api->request(MTPmessages_GetChatInviteImporters(
		MTP_flags(MTPmessages_GetChatInviteImporters::Flag::f_link),
		key.peer->input,
		MTP_string(key.link),
		MTPstring(), // q
		MTP_int(0), // offset_date
		MTP_inputUserEmpty(), // offset_user
		MTP_int(kJoinedFirstPage)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		_firstJoinedRequests.remove(key);
		_firstJoined[key] = ParseJoinedByLinkSlice(key.peer, result);
		_joinedFirstSliceLoaded.fire_copy(key);
	}).fail([=] {
		_firstJoinedRequests.remove(key);
	}).send();
	_firstJoinedRequests.emplace(key, requestId);
}

void InviteLinks::setMyPermanent(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) {
	auto link = parse(peer, invite);
	if (!link) {
		LOG(("API Error: "
			"InviteLinks::setPermanent called with non-link."));
		return;
	} else if (!link->permanent) {
		LOG(("API Error: "
			"InviteLinks::setPermanent called with non-permanent link."));
		return;
	}
	auto i = _firstSlices.find(peer);
	if (i == end(_firstSlices)) {
		i = _firstSlices.emplace(peer).first;
	}
	auto &links = i->second;
	auto updateOldPermanent = Update{
		.peer = peer,
		.admin = peer->session().user(),
	};
	if (const auto permanent = lookupMyPermanent(links)) {
		if (permanent->link == link->link) {
			if (permanent->usage != link->usage) {
				permanent->usage = link->usage;
				_updates.fire(Update{
					.peer = peer,
					.admin = peer->session().user(),
					.was = link->link,
					.now = *permanent
				});
			}
			return;
		}
		updateOldPermanent.was = permanent->link;
		updateOldPermanent.now = *permanent;
		updateOldPermanent.now->revoked = true;
		links.links.erase(begin(links.links));
		if (links.count > 0) {
			--links.count;
		}
	}
	links.links.insert(begin(links.links), *link);

	editPermanentLink(peer, link->link);
	notify(peer);

	if (updateOldPermanent.now) {
		_updates.fire(std::move(updateOldPermanent));
	}
	_updates.fire(Update{
		.peer = peer,
		.admin = peer->session().user(),
		.now = link
	});
}

void InviteLinks::clearMyPermanent(not_null<PeerData*> peer) {
	auto i = _firstSlices.find(peer);
	if (i == end(_firstSlices)) {
		return;
	}
	auto &links = i->second;
	const auto permanent = lookupMyPermanent(links);
	if (!permanent) {
		return;
	}

	auto updateOldPermanent = Update{
		.peer = peer,
		.admin = peer->session().user()
	};
	updateOldPermanent.was = permanent->link;
	updateOldPermanent.now = *permanent;
	updateOldPermanent.now->revoked = true;
	links.links.erase(begin(links.links));
	if (links.count > 0) {
		--links.count;
	}

	editPermanentLink(peer, QString());
	notify(peer);

	if (updateOldPermanent.now) {
		_updates.fire(std::move(updateOldPermanent));
	}
}

void InviteLinks::notify(not_null<PeerData*> peer) {
	peer->session().changes().peerUpdated(
		peer,
		Data::PeerUpdate::Flag::InviteLinks);
}

auto InviteLinks::myLinks(not_null<PeerData*> peer) const -> const Links & {
	static const auto kEmpty = Links();
	const auto i = _firstSlices.find(peer);
	return (i != end(_firstSlices)) ? i->second : kEmpty;
}

auto InviteLinks::parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const -> Links {
	auto i = _firstSlices.find(peer);
	const auto permanent = (i != end(_firstSlices))
		? lookupMyPermanent(i->second)
		: nullptr;
	auto result = Links();
	slice.match([&](const MTPDmessages_exportedChatInvites &data) {
		peer->session().data().processUsers(data.vusers());
		result.count = data.vcount().v;
		for (const auto &invite : data.vinvites().v) {
			if (const auto link = parse(peer, invite)) {
				if (!permanent || link->link != permanent->link) {
					result.links.push_back(*link);
				}
			}
		}
	});
	return result;
}

auto InviteLinks::parse(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) const -> std::optional<Link> {
	return invite.match([&](const MTPDchatInviteExported &data) {
		return std::optional<Link>(Link{
			.link = qs(data.vlink()),
			.label = qs(data.vtitle().value_or_empty()),
			.subscription = data.vsubscription_pricing()
				? Data::PeerSubscription{
					data.vsubscription_pricing()->data().vamount().v,
					data.vsubscription_pricing()->data().vperiod().v,
				}
				: Data::PeerSubscription(),
			.admin = peer->session().data().user(data.vadmin_id()),
			.date = data.vdate().v,
			.startDate = data.vstart_date().value_or_empty(),
			.expireDate = data.vexpire_date().value_or_empty(),
			.usageLimit = data.vusage_limit().value_or_empty(),
			.usage = data.vusage().value_or_empty(),
			.requested = data.vrequested().value_or_empty(),
			.requestApproval = data.is_request_needed(),
			.permanent = data.is_permanent(),
			.revoked = data.is_revoked(),
		});
	}, [&](const MTPDchatInvitePublicJoinRequests &data) {
		return std::optional<Link>();
	});
}

void InviteLinks::requestMoreLinks(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		TimeId lastDate,
		const QString &lastLink,
		bool revoked,
		Fn<void(Links)> done) {
	using Flag = MTPmessages_GetExportedChatInvites::Flag;
	_api->request(MTPmessages_GetExportedChatInvites(
		MTP_flags(Flag::f_offset_link
			| (revoked ? Flag::f_revoked : Flag(0))),
		peer->input,
		admin->inputUser,
		MTP_int(lastDate),
		MTP_string(lastLink),
		MTP_int(kPerPage)
	)).done([=](const MTPmessages_ExportedChatInvites &result) {
		done(parseSlice(peer, result));
	}).fail([=] {
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
