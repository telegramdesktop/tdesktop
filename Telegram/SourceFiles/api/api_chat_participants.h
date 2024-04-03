/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_chat_participant_status.h"
#include "mtproto/sender.h"
#include "base/timer.h"

class ApiWrap;
class ChannelData;

namespace Ui {
class Show;
} // namespace Ui

namespace Api {

class ChatParticipant final {
public:
	enum class Type {
		Creator,
		Admin,
		Member,
		Restricted,
		Left,
		Banned,
	};

	explicit ChatParticipant(
		const MTPChannelParticipant &p,
		not_null<PeerData*> peer);
	ChatParticipant(
		Type type,
		PeerId peerId,
		UserId by,
		ChatRestrictionsInfo restrictions,
		ChatAdminRightsInfo rights,
		bool canBeEdited = false,
		QString rank = QString());

	bool isUser() const;
	bool isCreator() const;
	bool isCreatorOrAdmin() const;
	bool isKicked() const;
	bool canBeEdited() const;

	UserId by() const;
	PeerId id() const;
	UserId userId() const;

	ChatRestrictionsInfo restrictions() const;
	ChatAdminRightsInfo rights() const;

	Type type() const;
	QString rank() const;

	void tryApplyCreatorTo(not_null<ChannelData*> channel) const;
private:
	Type _type = Type::Member;

	PeerId _peer;
	UserId _by; // Banned/Restricted/Promoted.

	bool _canBeEdited = false;

	QString _rank;

	ChatRestrictionsInfo _restrictions;
	ChatAdminRightsInfo _rights;
};

class ChatParticipants final {
public:
	struct Parsed {
		const int availableCount;
		const std::vector<ChatParticipant> list;
	};

	using TLMembers = MTPDchannels_channelParticipants;
	using Members = const std::vector<ChatParticipant> &;
	explicit ChatParticipants(not_null<ApiWrap*> api);

	void requestLast(not_null<ChannelData*> channel);
	void requestBots(not_null<ChannelData*> channel);
	void requestAdmins(not_null<ChannelData*> channel);
	void requestCountDelayed(not_null<ChannelData*> channel);

	static Parsed Parse(
		not_null<ChannelData*> channel,
		const TLMembers &data);
	static Parsed ParseRecent(
		not_null<ChannelData*> channel,
		const TLMembers &data);
	void add(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		const std::vector<not_null<UserData*>> &users,
		bool passGroupHistory = true,
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

	void loadSimilarChannels(not_null<ChannelData*> channel);

	struct Channels {
		std::vector<not_null<ChannelData*>> list;
		int more = 0;

		friend inline bool operator==(
			const Channels &,
			const Channels &) = default;
	};
	[[nodiscard]] const Channels &similar(not_null<ChannelData*> channel);
	[[nodiscard]] auto similarLoaded() const
		-> rpl::producer<not_null<ChannelData*>>;

private:
	struct SimilarChannels {
		Channels channels;
		mtpRequestId requestId = 0;
	};

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

	base::flat_map<not_null<ChannelData*>, SimilarChannels> _similar;
	rpl::event_stream<not_null<ChannelData*>> _similarLoaded;

};

} // namespace Api
