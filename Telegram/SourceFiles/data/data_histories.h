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

namespace MTP {
class Error;
struct Response;
} // namespace MTP

namespace Data {

class Session;
class Folder;
struct WebPageDraft;

[[nodiscard]] MTPInputReplyTo ReplyToForMTP(
	not_null<History*> history,
	FullReplyTo replyTo);
[[nodiscard]] MTPInputMedia WebPageForMTP(
	const Data::WebPageDraft &draft,
	bool required = false);

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

	void requestGroupAround(not_null<HistoryItem*> item);

	void deleteMessages(
		not_null<History*> history,
		const QVector<MTPint> &ids,
		bool revoke);
	void deleteAllMessages(
		not_null<History*> history,
		MsgId deleteTillId,
		bool justClear,
		bool revoke);

	void deleteMessagesByDates(
		not_null<History*> history,
		QDate firstDayToDelete,
		QDate lastDayToDelete,
		bool revoke);
	void deleteMessagesByDates(
		not_null<History*> history,
		TimeId minDate,
		TimeId maxDate,
		bool revoke);

	void deleteMessages(const MessageIdsList &ids, bool revoke);

	int sendRequest(
		not_null<History*> history,
		RequestType type,
		Fn<mtpRequestId(Fn<void()> finish)> generator);
	void cancelRequest(int id);

	using PreparedMessage = std::variant<
		MTPmessages_SendMessage,
		MTPmessages_SendMedia,
		MTPmessages_SendInlineBotResult,
		MTPmessages_SendMultiMedia>;
	int sendPreparedMessage(
		not_null<History*> history,
		FullReplyTo replyTo,
		uint64 randomId,
		Fn<PreparedMessage(not_null<History*>, FullReplyTo)> message,
		Fn<void(const MTPUpdates&, const MTP::Response&)> done,
		Fn<void(const MTP::Error&, const MTP::Response&)> fail);

	struct ReplyToPlaceholder {
	};
	template <typename RequestType, typename ...Args>
	static auto PrepareMessage(const Args &...args)
	-> Fn<Histories::PreparedMessage(not_null<History*>, FullReplyTo)> {
		return [=](not_null<History*> history, FullReplyTo replyTo)
		-> RequestType {
			return { ReplaceReplyIds(history, args, replyTo)... };
		};
	}

	void checkTopicCreated(FullMsgId rootId, MsgId realRoot);
	[[nodiscard]] FullMsgId convertTopicReplyToId(
		not_null<History*> history,
		FullMsgId replyToId) const;
	[[nodiscard]] MsgId convertTopicReplyToId(
		not_null<History*> history,
		MsgId replyToId) const;

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
	struct ChatListGroupRequest {
		MsgId aroundId = 0;
		mtpRequestId requestId = 0;
	};
	struct DelayedByTopicMessage {
		uint64 randomId = 0;
		FullMsgId replyTo;
		Fn<PreparedMessage(not_null<History*>, FullReplyTo)> message;
		Fn<void(const MTPUpdates&, const MTP::Response&)> done;
		Fn<void(const MTP::Error&, const MTP::Response&)> fail;
		int requestId = 0;
	};
	struct GroupRequestKey {
		not_null<History*> history;
		MsgId rootId = 0;

		friend inline auto operator<=>(
			GroupRequestKey,
			GroupRequestKey) = default;
	};

	template <typename Arg>
	static auto ReplaceReplyIds(
			not_null<History*> history,
			Arg arg,
			FullReplyTo replyTo) {
		if constexpr (std::is_same_v<Arg, ReplyToPlaceholder>) {
			return ReplyToForMTP(history, replyTo);
		} else {
			return arg;
		}
	}

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

	[[nodiscard]] bool isCreatingTopic(
		not_null<History*> history,
		MsgId rootId) const;
	void sendCreateTopicRequest(not_null<History*> history, MsgId rootId);
	void cancelDelayedByTopicRequest(int id);

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

	base::flat_map<
		GroupRequestKey,
		ChatListGroupRequest> _chatListGroupRequests;

	base::flat_map<
		FullMsgId,
		std::vector<DelayedByTopicMessage>> _creatingTopics;
	base::flat_map<FullMsgId, MsgId> _createdTopicIds;
	base::flat_set<mtpRequestId> _creatingTopicRequests;

};

} // namespace Data
