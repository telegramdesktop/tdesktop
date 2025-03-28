/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_set.h"
#include "base/timer.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>
#include <rpl/variable.h>

#include <crl/crl_time.h>

namespace TdE2E {

struct UserId {
	uint64 v = 0;

	friend inline constexpr auto operator<=>(UserId, UserId) = default;
	friend inline constexpr bool operator==(UserId, UserId) = default;
};

struct PrivateKeyId {
	uint64 v = 0;
};

struct CallId {
	uint64 v = 0;

	explicit operator bool() const {
		return v != 0;
	}
};

struct PublicKey {
	uint64 a = 0;
	uint64 b = 0;
	uint64 c = 0;
	uint64 d = 0;
};

struct ParticipantState {
	UserId id;
	PublicKey key;
};

struct ParticipantsSet {
	base::flat_set<UserId> list;

	friend inline bool operator==(
		const ParticipantsSet &,
		const ParticipantsSet &) = default;
};

struct Block {
	QByteArray data;
};

enum class CallFailure {
	Unknown,
};

class Call final {
public:
	explicit Call(UserId myUserId);
	~Call();

	[[nodiscard]] PublicKey myKey() const;

	void joined();
	void apply(
		int subchain,
		int index,
		const Block &block,
		bool fromShortPoll);

	struct SubchainRequest {
		int subchain = 0;
		int height = 0;
	};
	[[nodiscard]] rpl::producer<SubchainRequest> subchainRequests() const;
	void subchainBlocksRequestFinished(int subchain);

	[[nodiscard]] rpl::producer<QByteArray> sendOutboundBlock() const;

	[[nodiscard]] std::optional<CallFailure> failed() const;
	[[nodiscard]] rpl::producer<CallFailure> failures() const;

	[[nodiscard]] QByteArray emojiHash() const;
	[[nodiscard]] rpl::producer<QByteArray> emojiHashValue() const;

	void refreshLastBlock0(std::optional<Block> block);
	[[nodiscard]] Block makeJoinBlock();
	[[nodiscard]] Block makeRemoveBlock(const base::flat_set<UserId> &ids);

	[[nodiscard]] rpl::producer<ParticipantsSet> participantsSetValue() const;

	[[nodiscard]] auto callbackEncryptDecrypt()
		-> Fn<std::vector<uint8_t>(const std::vector<uint8_t>&, bool)>;

private:
	static constexpr int kSubChainsCount = 2;

	struct GuardedCallId {
		CallId value;
		std::atomic<bool> exists;
	};

	struct SubChainState {
		base::Timer shortPollTimer;
		base::Timer waitingTimer;
		crl::time lastUpdate = 0;
		base::flat_map<int, Block> waiting;
		bool shortPolling = true;
		int height = 0;
	};

	void setId(CallId id);
	void apply(int subchain, const Block &last);
	void fail(CallFailure reason);

	void checkForOutboundMessages();
	void checkWaitingBlocks(int subchain, bool waited = false);
	void shortPoll(int subchain);

	[[nodiscard]] std::int64_t libId() const;

	CallId _id;
	UserId _myUserId;
	PrivateKeyId _myKeyId;
	PublicKey _myKey;
	std::optional<CallFailure> _failure;
	rpl::event_stream<CallFailure> _failures;
	std::shared_ptr<GuardedCallId> _guardedId;

	SubChainState _subchains[kSubChainsCount];
	rpl::event_stream<SubchainRequest> _subchainRequests;
	rpl::event_stream<QByteArray> _outboundBlocks;

	std::optional<Block> _lastBlock0;
	int _lastBlock0Height = 0;

	rpl::variable<ParticipantsSet> _participantsSet;
	rpl::variable<QByteArray> _emojiHash;

};

} // namespace TdE2E
