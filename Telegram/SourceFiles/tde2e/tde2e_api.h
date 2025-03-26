/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/timer.h"

#include <rpl/producer.h>
#include <rpl/event_stream.h>

#include <crl/crl_time.h>

namespace TdE2E {

struct UserId {
	uint64 v = 0;
};

struct PrivateKeyId {
	uint64 v = 0;
};

struct CallId {
	uint64 v = 0;
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

struct Block {
	QByteArray data;
};

enum class CallFailure {
	Unknown,
};

class Call final {
public:
	explicit Call(UserId myUserId);

	[[nodiscard]] PublicKey myKey() const;

	[[nodiscard]] Block makeZeroBlock() const;

	void create(const Block &last);

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

	[[nodiscard]] std::optional<CallFailure> failed() const;
	[[nodiscard]] rpl::producer<CallFailure> failures() const;

	[[nodiscard]] std::vector<uint8_t> encrypt(
		const std::vector<uint8_t> &data) const;
	[[nodiscard]] std::vector<uint8_t> decrypt(
		const std::vector<uint8_t> &data) const;

private:
	static constexpr int kSubChainsCount = 2;

	struct SubChainState {
		base::Timer shortPollTimer;
		base::Timer waitingTimer;
		crl::time lastUpdate = 0;
		base::flat_map<int, Block> waiting;
		bool shortPolling = true;
		int height = 0;
	};

	void fail(CallFailure reason);

	void checkWaitingBlocks(int subchain, bool waited = false);
	void shortPoll(int subchain);

	CallId _id;
	UserId _myUserId;
	PrivateKeyId _myKeyId;
	PublicKey _myKey;
	std::optional<CallFailure> _failure;
	rpl::event_stream<CallFailure> _failures;

	SubChainState _subchains[kSubChainsCount];
	rpl::event_stream<SubchainRequest> _subchainRequests;

};

} // namespace TdE2E
