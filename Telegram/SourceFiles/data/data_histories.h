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
class Folder;

class Histories final {
public:
	enum class RequestType : uchar {
		None,
		History,
		ReadInbox,
		Delete,
		Send,
	};

	explicit Histories(not_null<Session*> owner);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] History *find(PeerId peerId);
	[[nodiscard]] not_null<History*> findOrCreate(PeerId peerId);

	void applyPeerDialogs(const MTPmessages_PeerDialogs &dialogs);

	void unloadAll();
	void clearAll();

	void readInbox(not_null<History*> history);
	void readInboxTill(not_null<HistoryItem*> item);
	void readInboxTill(not_null<History*> history, MsgId tillId);
	void readInboxOnNewMessage(not_null<HistoryItem*> item);
	void readClientSideMessage(not_null<HistoryItem*> item);
	void sendPendingReadInbox(not_null<History*> history);

	void requestDialogEntry(not_null<Data::Folder*> folder);
	void requestDialogEntry(
		not_null<History*> history,
		Fn<void()> callback = nullptr);
	void dialogEntryApplied(not_null<History*> history);
	void changeDialogUnreadMark(not_null<History*> history, bool unread);
	void requestFakeChatListMessage(not_null<History*> history);

	void deleteMessages(
		not_null<History*> history,
		const QVector<MTPint> &ids,
		bool revoke);
	void deleteAllMessages(
		not_null<History*> history,
		MsgId deleteTillId,
		bool justClear,
		bool revoke);

	void deleteMessages(const MessageIdsList &ids, bool revoke);

	int sendRequest(
		not_null<History*> history,
		RequestType type,
		Fn<mtpRequestId(Fn<void()> finish)> generator);
	void cancelRequest(int id);

private:
	struct PostponedHistoryRequest {
		Fn<mtpRequestId(Fn<void()> finish)> generator;
	};
	struct SentRequest {
		Fn<mtpRequestId(Fn<void()> finish)> generator;
		mtpRequestId id = 0;
		RequestType type = RequestType::None;
	};
	struct State {
		base::flat_map<int, PostponedHistoryRequest> postponed;
		base::flat_map<int, SentRequest> sent;
		MsgId willReadTill = 0;
		MsgId sentReadTill = 0;
		crl::time willReadWhen = 0;
		bool sentReadDone = false;
		bool postponedRequestEntry = false;
	};

	void readInboxTill(not_null<History*> history, MsgId tillId, bool force);
	void sendReadRequests();
	void sendReadRequest(not_null<History*> history, State &state);
	[[nodiscard]] State *lookup(not_null<History*> history);
	void checkEmptyState(not_null<History*> history);
	void checkPostponed(not_null<History*> history, int id);
	void finishSentRequest(
		not_null<History*> history,
		not_null<State*> state,
		int id);
	[[nodiscard]] bool postponeHistoryRequest(const State &state) const;
	[[nodiscard]] bool postponeEntryRequest(const State &state) const;
	void postponeRequestDialogEntries();

	void sendDialogRequests();

	const not_null<Session*> _owner;

	std::unordered_map<PeerId, std::unique_ptr<History>> _map;
	base::flat_map<not_null<History*>, State> _states;
	base::flat_map<int, not_null<History*>> _historyByRequest;
	int _requestAutoincrement = 0;
	base::Timer _readRequestsTimer;

	base::flat_set<not_null<Data::Folder*>> _dialogFolderRequests;
	base::flat_map<
		not_null<History*>,
		std::vector<Fn<void()>>> _dialogRequests;
	base::flat_map<
		not_null<History*>,
		std::vector<Fn<void()>>> _dialogRequestsPending;

	base::flat_set<not_null<History*>> _fakeChatListRequests;

};

} // namespace Data
