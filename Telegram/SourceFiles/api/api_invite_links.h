/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;

namespace Api {

struct InviteLink {
	QString link;
	not_null<UserData*> admin;
	TimeId date;
	TimeId expireDate = 0;
	int usageLimit = 0;
	int usage = 0;
	bool revoked = false;
};

struct PeerInviteLinks {
	std::vector<InviteLink> links;
	int count = 0;
};

class InviteLinks final {
public:
	explicit InviteLinks(not_null<ApiWrap*> api);

	using Link = InviteLink;
	using Links = PeerInviteLinks;

	void create(
		not_null<PeerData*> peer,
		TimeId expireDate = 0,
		int usageLimit = 0);
	void edit(
		not_null<PeerData*> peer,
		const QString &link,
		TimeId expireDate,
		int usageLimit);
	void revoke(not_null<PeerData*> peer, const QString &link);

	void requestLinks(not_null<PeerData*> peer);
	[[nodiscard]] Links links(not_null<PeerData*> peer) const;

	void requestMoreLinks(
		not_null<PeerData*> peer,
		const QString &last,
		Fn<void(Links)> done);

private:
	struct EditKey {
		not_null<PeerData*> peer;
		QString link;

		friend inline bool operator<(const EditKey &a, const EditKey &b) {
			return (a.peer == b.peer)
				? (a.link < b.link)
				: (a.peer < b.peer);
		}
	};

	void editPermanentLink(
		not_null<PeerData*> peer,
		const QString &from,
		const QString &to);
	[[nodiscard]] Links parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const;

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<PeerData*>, Links> _firstSlices;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _firstSliceRequests;

	base::flat_map<not_null<PeerData*>, mtpRequestId> _createRequests;
	base::flat_map<EditKey, mtpRequestId> _editRequests;

};

} // namespace Api
