/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Data {
class GroupCall;
struct PaidReactionSend;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace MTP {
class Sender;
struct Response;
} // namespace MTP

namespace Calls::Group {

struct Message {
	MsgId id = 0;
	TimeId date = 0;
	TimeId pinFinishDate = 0;
	not_null<PeerData*> peer;
	TextWithEntities text;
	int stars = 0;
	bool failed = false;
	bool admin = false;
	bool mine = false;
};

struct MessageIdUpdate {
	MsgId localId = 0;
	MsgId realId = 0;
};

struct MessageDeleteRequest {
	MsgId id = 0;
	PeerData *deleteAllFrom = nullptr;
	PeerData *ban = nullptr;
	bool reportSpam = false;
};

struct StarsDonor {
	PeerData *peer = nullptr;
	int stars = 0;
	bool my = false;

	friend inline bool operator==(
		const StarsDonor &,
		const StarsDonor &) = default;
};

struct StarsTop {
	std::vector<StarsDonor> topDonors;
	int total = 0;

	friend inline bool operator==(
		const StarsTop &,
		const StarsTop &) = default;
};

class Messages final : public base::has_weak_ptr {
public:
	Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api);
	~Messages();

	void send(TextWithTags text, int stars);

	void setApplyingInitial(bool value);
	void received(const MTPDupdateGroupCallMessage &data);
	void received(const MTPDupdateGroupCallEncryptedMessage &data);
	void deleted(const MTPDupdateDeleteGroupCallMessages &data);
	void sent(const MTPDupdateMessageID &data);

	[[nodiscard]] rpl::producer<std::vector<Message>> listValue() const;
	[[nodiscard]] rpl::producer<MessageIdUpdate> idUpdates() const;

	[[nodiscard]] int reactionsPaidScheduled() const;
	[[nodiscard]] PeerId reactionsLocalShownPeer() const;
	void reactionsPaidAdd(int count);
	void reactionsPaidScheduledCancel();
	void reactionsPaidSend();
	void undoScheduledPaidOnDestroy();

	struct PaidLocalState {
		int total = 0;
		int my = 0;
	};
	[[nodiscard]] PaidLocalState starsLocalState() const;
	[[nodiscard]] rpl::producer<StarsDonor> starsValueChanges() const {
		return _paidChanges.events();
	}
	[[nodiscard]] const StarsTop &starsTop() const {
		return _paid.top;
	}

	void requestHiddenShow() {
		_hiddenShowRequests.fire({});
	}
	[[nodiscard]] rpl::producer<> hiddenShowRequested() const {
		return _hiddenShowRequests.events();
	}

	void deleteConfirmed(MessageDeleteRequest request);

private:
	struct Pending {
		TextWithTags text;
		int stars = 0;
	};
	struct Paid {
		StarsTop top;
		PeerId scheduledShownPeer = 0;
		PeerId sendingShownPeer = 0;
		uint32 scheduled : 30 = 0;
		uint32 scheduledFlag : 1 = 0;
		uint32 scheduledPrivacySet : 1 = 0;
		uint32 sending : 30 = 0;
		uint32 sendingFlag : 1 = 0;
		uint32 sendingPrivacySet : 1 = 0;
	};

	[[nodiscard]] bool ready() const;
	void sendPending();
	void pushChanges();
	void checkDestroying(bool afterChanges = false);

	void received(
		MsgId id,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		TimeId date,
		int stars,
		bool fromAdmin,
		bool checkCustomEmoji = false);
	void sent(uint64 randomId, const MTP::Response &response);
	void sent(uint64 randomId, MsgId realId);
	void failed(uint64 randomId, const MTP::Response &response);

	[[nodiscard]] bool skipMessage(
		const TextWithEntities &text,
		int stars) const;
	[[nodiscard]] Data::PaidReactionSend startPaidReactionSending();
	void finishPaidSending(
		Data::PaidReactionSend send,
		bool success);
	void addStars(not_null<PeerData*> from, int stars, bool mine);
	void requestStarsStats();

	const not_null<GroupCall*> _call;
	const not_null<Main::Session*> _session;
	const not_null<MTP::Sender*> _api;

	MsgId _conferenceIdAutoIncrement = 0;
	base::flat_map<uint64, MsgId> _conferenceIdByRandomId;

	base::flat_map<uint64, MsgId> _sendingIdByRandomId;

	Data::GroupCall *_real = nullptr;

	std::vector<Pending> _pending;

	base::Timer _destroyTimer;
	std::vector<Message> _messages;
	base::flat_set<MsgId> _skippedIds;
	rpl::event_stream<std::vector<Message>> _changes;
	rpl::event_stream<MessageIdUpdate> _idUpdates;
	bool _applyingInitial = false;

	mtpRequestId _starsTopRequestId = 0;
	Paid _paid;
	rpl::event_stream<StarsDonor> _paidChanges;
	bool _paidSendingPending = false;

	TimeId _ttl = 0;
	bool _changesScheduled = false;

	rpl::event_stream<> _hiddenShowRequests;
	base::Timer _starsStatsTimer;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
