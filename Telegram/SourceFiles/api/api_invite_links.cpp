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

} // namespace

InviteLinks::InviteLinks(not_null<ApiWrap*> api) : _api(api) {
}

void InviteLinks::create(
		not_null<PeerData*> peer,
		TimeId expireDate,
		int usageLimit) {
	if (_createRequests.contains(peer)) {
		return;
	}

	using Flag = MTPmessages_ExportChatInvite::Flag;
	const auto requestId = _api->request(MTPmessages_ExportChatInvite(
		MTP_flags((expireDate ? Flag::f_expire_date : Flag(0))
			| (usageLimit ? Flag::f_usage_limit : Flag(0))),
		peer->input,
		MTP_int(expireDate),
		MTP_int(usageLimit)
	)).done([=](const MTPExportedChatInvite &result) {
		_createRequests.erase(peer);
		const auto link = (result.type() == mtpc_chatInviteExported)
			? qs(result.c_chatInviteExported().vlink())
			: QString();
		if (!expireDate && !usageLimit) {
			editPermanentLink(peer, QString(), link);
		}
	}).fail([=](const RPCError &error) {
		_createRequests.erase(peer);
	}).send();
	_createRequests.emplace(peer, requestId);
}

void InviteLinks::edit(
		not_null<PeerData*> peer,
		const QString &link,
		TimeId expireDate,
		int usageLimit) {
	const auto key = EditKey{ peer, link };
	if (_editRequests.contains(key)) {
		return;
	}

	using Flag = MTPmessages_EditExportedChatInvite::Flag;
	const auto requestId = _api->request(MTPmessages_EditExportedChatInvite(
		MTP_flags((expireDate ? Flag::f_expire_date : Flag(0))
			| (usageLimit ? Flag::f_usage_limit : Flag(0))),
		peer->input,
		MTP_string(link),
		MTP_int(expireDate),
		MTP_int(usageLimit)
	)).done([=](const MTPmessages_ExportedChatInvite &result) {
		_editRequests.erase(key);
		result.match([&](const MTPDmessages_exportedChatInvite &data) {
			_api->session().data().processUsers(data.vusers());
			const auto &invite = data.vinvite();
			const auto link = (invite.type() == mtpc_chatInviteExported)
				? qs(invite.c_chatInviteExported().vlink())
				: QString();
			// #TODO links
		});
	}).fail([=](const RPCError &error) {
		_editRequests.erase(key);
	}).send();
	_editRequests.emplace(key, requestId);
}

void InviteLinks::revoke(not_null<PeerData*> peer, const QString &link) {
	const auto key = EditKey{ peer, link };
	if (_editRequests.contains(key)) {
		return;
	}

	const auto requestId = _api->request(MTPmessages_EditExportedChatInvite(
		MTP_flags(MTPmessages_EditExportedChatInvite::Flag::f_revoked),
		peer->input,
		MTP_string(link),
		MTPint(), // expire_date
		MTPint() // usage_limit
	)).done([=](const MTPmessages_ExportedChatInvite &result) {
		_editRequests.erase(key);
		result.match([&](const MTPDmessages_exportedChatInvite &data) {
			_api->session().data().processUsers(data.vusers());
			const auto &invite = data.vinvite();
			const auto link = (invite.type() == mtpc_chatInviteExported)
				? qs(invite.c_chatInviteExported().vlink())
				: QString();
			editPermanentLink(peer, key.link, link);
		});
	}).fail([=](const RPCError &error) {
		_editRequests.erase(key);
	}).send();
	_editRequests.emplace(key, requestId);
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
		_firstSlices.emplace_or_assign(peer, parseSlice(peer, result));
		peer->session().changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::InviteLink);
	}).fail([=](const RPCError &error) {
		_firstSliceRequests.remove(peer);
	}).send();
	_firstSliceRequests.emplace(peer, requestId);
}

auto InviteLinks::links(not_null<PeerData*> peer) const -> Links {
	const auto i = _firstSlices.find(peer);
	return (i != end(_firstSlices)) ? i->second : Links();
}

auto InviteLinks::parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const -> Links {
	auto result = Links();
	slice.match([&](const MTPDmessages_exportedChatInvites &data) {
		auto &owner = peer->session().data();
		owner.processUsers(data.vusers());
		result.count = data.vcount().v;
		for (const auto &invite : data.vinvites().v) {
			invite.match([&](const MTPDchatInviteExported &data) {
				result.links.push_back({
					.link = qs(data.vlink()),
					.admin = owner.user(data.vadmin_id().v),
					.date = data.vdate().v,
					.expireDate = data.vexpire_date().value_or_empty(),
					.usageLimit = data.vusage_limit().value_or_empty(),
					.usage = data.vusage().value_or_empty(),
					.revoked = data.is_revoked(),
				});
			});
		}
	});
	return result;
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
		done(parseSlice(peer, result));
	}).fail([=](const RPCError &error) {
		done(Links());
	}).send();
}

void InviteLinks::editPermanentLink(
		not_null<PeerData*> peer,
		const QString &from,
		const QString &to) {
	if (const auto chat = peer->asChat()) {
		if (chat->inviteLink() == from) {
			chat->setInviteLink(to);
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->inviteLink() == from) {
			channel->setInviteLink(to);
		}
	} else {
		Unexpected("Peer in InviteLinks::editMainLink.");
	}
}

} // namespace Api
