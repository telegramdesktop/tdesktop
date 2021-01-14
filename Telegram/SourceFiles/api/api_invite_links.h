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
	bool permanent = false;
	bool expired = false;
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
		Fn<void(Link)> done = nullptr,
		TimeId expireDate = 0,
		int usageLimit = 0);
	void edit(
		not_null<PeerData*> peer,
		const QString &link,
		TimeId expireDate,
		int usageLimit,
		Fn<void(Link)> done = nullptr);
	void revoke(
		not_null<PeerData*> peer,
		const QString &link,
		Fn<void(Link)> done = nullptr);

	void setPermanent(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite);
	void clearPermanent(not_null<PeerData*> peer);

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

	[[nodiscard]] Links parseSlice(
		not_null<PeerData*> peer,
		const MTPmessages_ExportedChatInvites &slice) const;
	[[nodiscard]] Link parse(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite) const;
	[[nodiscard]] Link *lookupPermanent(not_null<PeerData*> peer);
	[[nodiscard]] Link *lookupPermanent(Links &links);
	[[nodiscard]] const Link *lookupPermanent(const Links &links) const;
	Link prepend(
		not_null<PeerData*> peer,
		const MTPExportedChatInvite &invite);
	void notify(not_null<PeerData*> peer);

	void editPermanentLink(
		not_null<PeerData*> peer,
		const QString &link);

	void performEdit(
		not_null<PeerData*> peer,
		const QString &link,
		Fn<void(Link)> done,
		bool revoke,
		TimeId expireDate = 0,
		int usageLimit = 0);
	void performCreate(
		not_null<PeerData*> peer,
		Fn<void(Link)> done,
		bool revokeLegacyPermanent,
		TimeId expireDate = 0,
		int usageLimit = 0);

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<PeerData*>, Links> _firstSlices;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _firstSliceRequests;

	base::flat_map<
		not_null<PeerData*>,
		std::vector<Fn<void(Link)>>> _createCallbacks;
	base::flat_map<EditKey, std::vector<Fn<void(Link)>>> _editCallbacks;

};

} // namespace Api
