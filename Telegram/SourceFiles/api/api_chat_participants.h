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

namespace Main {
class Session;
} // namespace Main

namespace Api {

class ChatParticipants final {
public:
	explicit ChatParticipants(not_null<ApiWrap*> api);

	void requestLast(not_null<ChannelData*> channel);
	void requestBots(not_null<ChannelData*> channel);
	void requestAdmins(not_null<ChannelData*> channel);
	void requestCountDelayed(not_null<ChannelData*> channel);

	void parse(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList,
		Fn<void()> callbackNotModified = nullptr);
	void parseRecent(
		not_null<ChannelData*> channel,
		const MTPchannels_ChannelParticipants &result,
		Fn<void(
			int availableCount,
			const QVector<MTPChannelParticipant> &list)> callbackList = nullptr,
		Fn<void()> callbackNotModified = nullptr);
	void add(
		not_null<PeerData*> peer,
		const std::vector<not_null<UserData*>> &users,
		Fn<void(bool)> done = nullptr);

	void requestSelf(not_null<ChannelData*> channel);

	void requestForAdd(
		not_null<ChannelData*> channel,
		Fn<void(const MTPchannels_ChannelParticipants&)> callback);

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
	void applyLastList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list);
	void applyBotsList(
		not_null<ChannelData*> channel,
		int availableCount,
		const QVector<MTPChannelParticipant> &list);

	void refreshChannelAdmins(
		not_null<ChannelData*> channel,
		const QVector<MTPChannelParticipant> &participants);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	using PeerRequests = base::flat_map<PeerData*, mtpRequestId>;

	PeerRequests _participantsRequests;
	PeerRequests _botsRequests;
	PeerRequests _adminsRequests;
	base::DelayedCallTimer _participantsCountRequestTimer;

	ChannelData *_channelMembersForAdd = nullptr;
	mtpRequestId _channelMembersForAddRequestId = 0;
	Fn<void(
		const MTPchannels_ChannelParticipants&)> _channelMembersForAddCallback;

	base::flat_set<not_null<ChannelData*>> _selfParticipantRequests;

	using KickRequest = std::pair<
		not_null<ChannelData*>,
		not_null<PeerData*>>;
	base::flat_map<KickRequest, mtpRequestId> _kickRequests;

};

} // namespace Api
