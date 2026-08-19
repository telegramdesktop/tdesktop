/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class PeerData;
class UserData;
class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

inline constexpr auto kCommunityRequestCreated
	= "COMMUNITY_REQUEST_CREATED"_cs;
inline constexpr auto kCommunityPeersTooMuch
	= "COMMUNITY_PEERS_TOO_MUCH"_cs;

struct CommunityPeerRequest {
	not_null<PeerData*> peer;
	UserData *requestedBy = nullptr;
	TimeId date = 0;
	bool visible = false;
};

struct CommunityPeerRequestsSlice {
	std::vector<CommunityPeerRequest> list;
	QString nextOffset;
	int totalCount = 0;
};

struct CommunityParticipantJoinedChats {
	std::vector<not_null<PeerData*>> creatorChats;
	std::vector<not_null<PeerData*>> joinedChats;
};

enum class PeerLinkAction {
	Visible,
	Hidden,
	Deleted,
};

class Communities final {
public:
	explicit Communities(not_null<ApiWrap*> api);

	void create(
		const QString &title,
		const QString &about,
		not_null<PeerData*> peer,
		bool hidden,
		Fn<void(not_null<ChannelData*>)> done,
		Fn<void(const QString &)> fail);

	void addPeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		bool visible,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void removePeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		Fn<void()> done,
		Fn<void(const QString &)> fail);

	void requestJoinedCommunities(
		Fn<void(const std::vector<not_null<ChannelData*>> &)> done);

	void toggleCollapsedInDialogs(
		not_null<ChannelData*> community,
		bool collapsed);

	void requestPeerLinkRequests(
		not_null<ChannelData*> community,
		const QString &offset,
		int limit,
		Fn<void(CommunityPeerRequestsSlice)> done);

	void togglePeerLinkRequestApproval(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		bool reject,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void toggleAllPeerLinkRequestApproval(
		not_null<ChannelData*> community,
		bool reject,
		Fn<void()> done,
		Fn<void(const QString &)> fail);

	void toggleParticipantBanned(
		not_null<ChannelData*> community,
		not_null<PeerData*> participant,
		bool unban,
		Fn<void()> done,
		Fn<void(const QString &)> fail);

	void requestParticipantJoinedChats(
		not_null<ChannelData*> community,
		not_null<PeerData*> participant,
		Fn<void(CommunityParticipantJoinedChats)> done);

private:
	void togglePeerLink(
		not_null<ChannelData*> community,
		not_null<PeerData*> peer,
		PeerLinkAction action,
		Fn<void()> done,
		Fn<void(const QString &)> fail);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_set<not_null<ChannelData*>> _collapseRequests;
	base::flat_map<
		not_null<ChannelData*>,
		mtpRequestId> _peerLinkRequestsRequests;
	mtpRequestId _joinedRequestId = 0;

};

[[nodiscard]] int CommunityPeersLimit(not_null<Main::Session*> session);
[[nodiscard]] int CommunityBotPeersLimit(not_null<Main::Session*> session);
[[nodiscard]] QString CommunityPeersLimitToast(not_null<PeerData*> peer);

} // namespace Api
