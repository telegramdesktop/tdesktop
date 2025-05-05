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
#include <rpl/lifetime.h>
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

using EncryptionBuffer = std::vector<uint8_t>;

class EncryptDecrypt final
	: public std::enable_shared_from_this<EncryptDecrypt> {
public:
	[[nodiscard]] auto callback()
	-> Fn<EncryptionBuffer(const EncryptionBuffer&, int64_t, bool, int32_t)>;

	void setCallId(CallId id);
	void clearCallId(CallId fromId);

private:
	std::atomic<uint64> _id = 0;

};

class Call final {
public:
	explicit Call(UserId myUserId);
	~Call();

	[[nodiscard]] PublicKey myKey() const;

	void joined();
	void apply(
		int subchain,
		int indexAfterLast,
		const std::vector<Block> &blocks,
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

	[[nodiscard]] bool hasLastBlock0() const;
	void refreshLastBlock0(std::optional<Block> block);
	[[nodiscard]] Block makeJoinBlock();
	[[nodiscard]] Block makeRemoveBlock(const base::flat_set<UserId> &ids);

	[[nodiscard]] auto participantsSetValue() const
		-> rpl::producer<ParticipantsSet>;

	void registerEncryptDecrypt(std::shared_ptr<EncryptDecrypt> object);

	[[nodiscard]] rpl::lifetime &lifetime();

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
	std::shared_ptr<EncryptDecrypt> _encryptDecrypt;

	SubChainState _subchains[kSubChainsCount];
	rpl::event_stream<SubchainRequest> _subchainRequests;
	rpl::event_stream<QByteArray> _outboundBlocks;

	std::optional<Block> _lastBlock0;
	int _lastBlock0Height = 0;

	rpl::variable<ParticipantsSet> _participantsSet;
	rpl::variable<QByteArray> _emojiHash;

	rpl::lifetime _lifetime;

};

} // namespace TdE2E
