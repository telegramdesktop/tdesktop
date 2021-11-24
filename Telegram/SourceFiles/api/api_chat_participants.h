/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/timer.h"

class ApiWrap;
class ChannelData;

struct ChatRestrictionsInfo;

namespace Api {

class ChatParticipants final {
public:
	using TLMembers = MTPchannels_ChannelParticipants;
	using TLMembersList = const QVector<MTPChannelParticipant> &;
	explicit ChatParticipants(not_null<ApiWrap*> api);

	void requestLast(not_null<ChannelData*> channel);
	void requestBots(not_null<ChannelData*> channel);
	void requestAdmins(not_null<ChannelData*> channel);
	void requestCountDelayed(not_null<ChannelData*> channel);

	void parse(
		not_null<ChannelData*> channel,
		const TLMembers &result,
		Fn<void(int availableCount, TLMembersList list)> callbackList);
	void parseRecent(
		not_null<ChannelData*> channel,
		const TLMembers &result,
		Fn<void(int availableCount, TLMembersList)> callbackList = nullptr);
	void add(
		not_null<PeerData*> peer,
		const std::vector<not_null<UserData*>> &users,
		Fn<void(bool)> done = nullptr);

	void requestSelf(not_null<ChannelData*> channel);

	void requestForAdd(
		not_null<ChannelData*> channel,
		Fn<void(const TLMembers&)> callback);

	void kick(
		not_null<ChatData*> chat,
		not_null<PeerData*> participant);
	void kick(
		not_null<ChannelData*> channel,
		not_null<PeerData*> participant,
		ChatRestrictionsInfo currentRights);
	void unblock(
		not_null<ChannelData*> channel,
		not_null<PeerData*> participant);

private:
	MTP::Sender _api;

	using PeerRequests = base::flat_map<PeerData*, mtpRequestId>;

	PeerRequests _participantsRequests;
	PeerRequests _botsRequests;
	PeerRequests _adminsRequests;
	base::DelayedCallTimer _participantsCountRequestTimer;

	struct {
		ChannelData *channel = nullptr;
		mtpRequestId requestId = 0;
		Fn<void(const TLMembers&)> callback;
	} _forAdd;

	base::flat_set<not_null<ChannelData*>> _selfParticipantRequests;

	using KickRequest = std::pair<
		not_null<ChannelData*>,
		not_null<PeerData*>>;
	base::flat_map<KickRequest, mtpRequestId> _kickRequests;

};

} // namespace Api
