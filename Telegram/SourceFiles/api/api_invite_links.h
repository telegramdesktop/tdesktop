/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;

#include "data/data_subscriptions.h"

namespace Api {

struct InviteLink {
	QString link;
	QString label;
	Data::PeerSubscription subscription;
	not_null<UserData*> admin;
	TimeId date = 0;
	TimeId startDate = 0;
	TimeId expireDate = 0;
	int usageLimit = 0;
	int usage = 0;
	int requested = 0;
	bool requestApproval = false;
	bool permanent = false;
	bool revoked = false;
};

struct PeerInviteLinks {
	std::vector<InviteLink> links;
	int count = 0;
};

struct JoinedByLinkUser {
	not_null<UserData*> user;
	TimeId date = 0;
	bool viaFilterLink = false;
};

struct JoinedByLinkSlice {
	std::vector<JoinedByLinkUser> users;
	int count = 0;
};

struct InviteLinkUpdate {
	not_null<PeerData*> peer;
	not_null<UserData*> admin;
	QString was;
	std::optional<InviteLink> now;
};

[[nodiscard]] JoinedByLinkSlice ParseJoinedByLinkSlice(
	not_null<PeerData*> peer,
	const MTPmessages_ChatInviteImporters &slice);

struct CreateInviteLinkArgs {
	not_null<PeerData*> peer;
	Fn<void(InviteLink)> done = nullptr;
	QString label;
	TimeId expireDate = 0;
	int usageLimit = 0;
	bool requestApproval = false;
	Data::PeerSubscription subscription;
};

class InviteLinks final {
public:
	explicit InviteLinks(not_null<ApiWrap*> api);

	using Link = InviteLink;
	using Links = PeerInviteLinks;
	using Update = InviteLinkUpdate;

	void create(const CreateInviteLinkArgs &args);
	void edit(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		const QString &label,
		TimeId expireDate,
		int usageLimit,
		bool requestApproval,
		Fn<void(Link)> done = nullptr);
	void editTitle(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		const QString &label,
		Fn<void(Link)> done = nullptr);
	void revoke(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void(Link)> done = nullptr);
	void revokePermanent(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void()> done = nullptr);
	void destroy(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void()> done = nullptr);
	void destroyAllRevoked(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		Fn<void()> done = nullptr);

	void setMyPermanent(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite);
	void clearMyPermanent(not_null<PeerData*> peer);

	void requestMyLinks(not_null<PeerData*> peer);
	[[nodiscard]] const Links &myLinks(not_null<PeerData*> peer) const;

	void processRequest(
		not_null<PeerData*> peer,
		const QString &link,
		not_null<UserData*> user,
		bool approved,
		Fn<void()> done,
		Fn<void()> fail);
	void applyExternalUpdate(not_null<PeerData*> peer, InviteLink updated);

	[[nodiscard]] rpl::producer<JoinedByLinkSlice> joinedFirstSliceValue(
		not_null<PeerData*> peer,
		const QString &link,
		int fullCount);
	[[nodiscard]] std::optional<JoinedByLinkSlice> joinedFirstSliceLoaded(
		not_null<PeerData*> peer,
		const QString &link) const;
	[[nodiscard]] rpl::producer<Update> updates(
		not_null<PeerData*> peer,
		not_null<UserData*> admin) const;
	[[nodiscard]] rpl::producer<> allRevokedDestroyed(
		not_null<PeerData*> peer,
		not_null<UserData*> admin) const;

	void requestMoreLinks(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		TimeId lastDate,
		const QString &lastLink,
		bool revoked,
		Fn<void(Links)> done);

private:
	struct LinkKey {
		not_null<PeerData*> peer;
		QString link;

		friend inline bool operator<(const LinkKey &a, const LinkKey &b) {
			return (a.peer == b.peer)
				? (a.link < b.link)
				: (a.peer < b.peer);
		}
		friend inline bool operator==(const LinkKey &a, const LinkKey &b) {
			return (a.peer == b.peer) && (a.link == b.link);
		}
	};
	struct ProcessRequest {
		Fn<void()> done;
		Fn<void()> fail;
	};

	[[nodiscard]] Links parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const;
	[[nodiscard]] std::optional<Link> parse(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) const;
	[[nodiscard]] Link *lookupMyPermanent(not_null<PeerData*> peer);
	[[nodiscard]] Link *lookupMyPermanent(Links &links);
	[[nodiscard]] const Link *lookupMyPermanent(const Links &links) const;
	std::optional<Link> prepend(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const MTPExportedChatInvite &invite);
	void prependMyToFirstSlice(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const Link &link);
	void notify(not_null<PeerData*> peer);

	void editPermanentLink(
		not_null<PeerData*> peer,
		const QString &link);

	void performEdit(
		not_null<PeerData*> peer,
		not_null<UserData*> admin,
		const QString &link,
		Fn<void(Link)> done,
		bool revoke,
		const QString &label = QString(),
		TimeId expireDate = 0,
		int usageLimit = 0,
		bool requestApproval = false,
		bool editOnlyTitle = false);
	void performCreate(
		const CreateInviteLinkArgs &args,
		bool revokeLegacyPermanent);

	void requestJoinedFirstSlice(LinkKey key);
	[[nodiscard]] std::optional<JoinedByLinkSlice> lookupJoinedFirstSlice(
		LinkKey key) const;

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<PeerData*>, Links> _firstSlices;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _firstSliceRequests;

	base::flat_map<LinkKey, JoinedByLinkSlice> _firstJoined;
	base::flat_map<LinkKey, mtpRequestId> _firstJoinedRequests;
	rpl::event_stream<LinkKey> _joinedFirstSliceLoaded;

	base::flat_map<
		not_null<PeerData*>,
		std::vector<Fn<void(Link)>>> _createCallbacks;
	base::flat_map<LinkKey, std::vector<Fn<void(Link)>>> _editCallbacks;
	base::flat_map<LinkKey, std::vector<Fn<void()>>> _deleteCallbacks;
	base::flat_map<
		not_null<PeerData*>,
		std::vector<Fn<void()>>> _deleteRevokedCallbacks;

	base::flat_map<
		std::pair<not_null<PeerData*>, not_null<UserData*>>,
		ProcessRequest> _processRequests;

	rpl::event_stream<Update> _updates;

	struct AllRevokedDestroyed {
		not_null<PeerData*> peer;
		not_null<UserData*> admin;
	};
	rpl::event_stream<AllRevokedDestroyed> _allRevokedDestroyed;

};

} // namespace Api
