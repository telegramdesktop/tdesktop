/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class History;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

class Histories final {
public:
	explicit Histories(not_null<Session*> owner);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] History *find(PeerId peerId);
	[[nodiscard]] not_null<History*> findOrCreate(PeerId peerId);

	void unloadAll();
	void clearAll();

	void readInboxTill(
		not_null<History*> history,
		not_null<HistoryItem*> item);
	void readInboxTill(not_null<History*> history, MsgId tillId);
	void sendPendingReadInbox(not_null<History*> history);

private:
	enum class RequestType : uchar {
		None,
		DialogsEntry,
		History,
		ReadInbox,
		Delete,
	};
	enum class Action : uchar {
		Send,
		Postpone,
		Skip,
	};
	struct PostponedRequest {
		Fn<mtpRequestId(Fn<void()> done)> generator;
		RequestType type = RequestType::None;
	};
	struct SentRequest {
		mtpRequestId id = 0;
		RequestType type = RequestType::None;
	};
	struct State {
		base::flat_map<int, PostponedRequest> postponed;
		base::flat_map<int, SentRequest> sent;
		crl::time readWhen = 0;
		MsgId readTill = 0;
		int autoincrement = 0;
		bool thenRequestEntry = false;
	};

	void sendReadRequests();
	void sendReadRequest(not_null<History*> history, State &state);
	[[nodiscard]] State *lookup(not_null<History*> history);
	void checkEmptyState(not_null<History*> history);
	int sendRequest(
		not_null<History*> history,
		RequestType type,
		Fn<mtpRequestId(Fn<void()> done)> generator);
	void checkPostponed(not_null<History*> history, int requestId);
	[[nodiscard]] Action chooseAction(
		State &state,
		RequestType type,
		bool fromPostponed = false) const;

	const not_null<Session*> _owner;

	std::unordered_map<PeerId, std::unique_ptr<History>> _map;
	base::flat_map<not_null<History*>, State> _states;
	base::Timer _readRequestsTimer;

};

} // namespace Data
