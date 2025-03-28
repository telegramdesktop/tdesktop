/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tde2e/tde2e_api.h"

#include "base/assertion.h"
#include "base/debug_log.h"

#include <tde2e/td/e2e/e2e_api.h>

#define LOG_ERROR(error) \
	LOG(("TdE2E Error %1: %2").arg(int(error.code)).arg(error.message.c_str()))

#define LOG_AND_FAIL(error, reason) \
	LOG_ERROR(error); \
	fail(reason)

namespace TdE2E {
namespace {

constexpr auto kPermissionAdd = 1;
constexpr auto kPermissionRemove = 2;
constexpr auto kShortPollChainBlocksTimeout = 5 * crl::time(1000);
constexpr auto kShortPollChainBlocksWaitFor = crl::time(1000);

[[nodiscard]] tde2e_api::Slice Slice(const QByteArray &data) {
	return {
		data.constData(),
		std::string_view::size_type(data.size())
	};
}

[[nodiscard]] tde2e_api::Slice Slice(const std::vector<uint8_t> &data) {
	return {
		reinterpret_cast<const char*>(data.data()),
		std::string_view::size_type(data.size()),
	};
}

[[nodiscard]] ParticipantsSet ParseParticipantsSet(
		const tde2e_api::CallState &state) {
	auto result = ParticipantsSet();
	const auto &list = state.participants;
	result.list.reserve(list.size());
	for (const auto &entry : list) {
		result.list.emplace(UserId{ uint64(entry.user_id) });
	}
	return result;
}

} // namespace

Call::Call(UserId myUserId)
: _myUserId(myUserId) {
	const auto id = tde2e_api::key_generate_temporary_private_key();
	Assert(id.is_ok());
	_myKeyId = { .v = uint64(id.value()) };

	const auto key = tde2e_api::key_to_public_key(_myKeyId.v);
	Assert(key.is_ok());
	Assert(key.value().size() == sizeof(_myKey));
	memcpy(&_myKey, key.value().data(), sizeof(_myKey));
}

Call::~Call() {
	if (const auto id = libId()) {
		tde2e_api::call_destroy(id);
	}
}

void Call::fail(CallFailure reason) {
	_emojiHash = QByteArray();
	_failure = reason;
	_failures.fire_copy(reason);
}

PublicKey Call::myKey() const {
	return _myKey;
}

void Call::refreshLastBlock0(std::optional<Block> block) {
	_lastBlock0 = std::move(block);
}

Block Call::makeJoinBlock() {
	if (failed()) {
		return {};
	}

	const auto publicKeyView = std::string_view{
		reinterpret_cast<const char*>(&_myKey),
		sizeof(_myKey),
	};
	const auto publicKeyId = tde2e_api::key_from_public_key(publicKeyView);
	Assert(publicKeyId.is_ok());

	const auto myKeyId = std::int64_t(_myKeyId.v);
	const auto myParticipant = tde2e_api::CallParticipant{
		.user_id = std::int64_t(_myUserId.v),
		.public_key_id = publicKeyId.value(),
		.permissions = kPermissionAdd | kPermissionRemove,
	};

	const auto result = _lastBlock0
		? tde2e_api::call_create_self_add_block(
			myKeyId,
			Slice(_lastBlock0->data),
			myParticipant)
		: tde2e_api::call_create_zero_block(myKeyId, {
			.height = 0,
			.participants = { myParticipant },
		});
	if (!result.is_ok()) {
		LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		return {};
	}

	return {
		.data = QByteArray::fromStdString(result.value()),
	};
}

Block Call::makeRemoveBlock(UserId id) {
	if (failed() || !_id || id == _myUserId) {
		return {};
	}

	auto state = tde2e_api::call_get_state(libId());
	if (!state.is_ok()) {
		LOG_AND_FAIL(state.error(), CallFailure::Unknown);
		return {};
	}
	auto found = false;
	auto updated = state.value();
	auto &list = updated.participants;
	for (auto i = begin(list); i != end(list); ++i) {
		if (uint64(i->user_id) == id.v) {
			list.erase(i);
			found = true;
			break;
		}
	}
	if (!found) {
		return {};
	}
	const auto result = tde2e_api::call_create_change_state_block(
		libId(),
		updated);
	if (!result.is_ok()) {
		LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		return {};
	}
	return {
		.data = QByteArray::fromStdString(result.value()),
	};
}

rpl::producer<ParticipantsSet> Call::participantsSetValue() const {
	return _participantsSet.value();
}

void Call::joined() {
	shortPoll(0);
	if (_id) {
		shortPoll(1);
	}
}

void Call::apply(int subchain, const Block &last) {
	Expects(_id || !subchain);

	auto verification = std::optional<tde2e_api::CallVerificationState>();
	const auto guard = gsl::finally([&] {
		if (failed() || !_id) {
			return;
		} else if (!verification) {
			const auto id = libId();
			auto result = tde2e_api::call_get_verification_state(id);
			if (!result.is_ok()) {
				LOG_AND_FAIL(result.error(), CallFailure::Unknown);
				return;
			}
			verification = std::move(result.value());
		}
		_emojiHash = verification->emoji_hash.has_value()
			? QByteArray::fromStdString(*verification->emoji_hash)
			: QByteArray();
		checkForOutboundMessages();
	});

	if (subchain) {
		auto result = tde2e_api::call_receive_inbound_message(
			libId(),
			Slice(last.data));
		if (!result.is_ok()) {
			LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		} else {
			verification = std::move(result.value());
		}
		return;
	} else if (_id) {
		const auto result = tde2e_api::call_apply_block(
			libId(),
			Slice(last.data));
		if (!result.is_ok()) {
			LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		} else {
			_participantsSet = ParseParticipantsSet(result.value());
		}
		return;
	}
	const auto id = tde2e_api::call_create(
		std::int64_t(_myKeyId.v),
		Slice(last.data));
	if (!id.is_ok()) {
		LOG_AND_FAIL(id.error(), CallFailure::Unknown);
		return;
	}
	_id = CallId{ uint64(id.value()) };

	for (auto i = 0; i != kSubChainsCount; ++i) {
		auto &entry = _subchains[i];
		entry.waitingTimer.setCallback([=] {
			checkWaitingBlocks(i, true);
		});
		entry.shortPollTimer.setCallback([=] {
			shortPoll(i);
		});
		if (!entry.waitingTimer.isActive()) {
			entry.shortPollTimer.callOnce(kShortPollChainBlocksTimeout);
		}
	}

	const auto state = tde2e_api::call_get_state(libId());
	if (!state.is_ok()) {
		LOG_AND_FAIL(state.error(), CallFailure::Unknown);
		return;
	}
	_participantsSet = ParseParticipantsSet(state.value());
}

void Call::checkForOutboundMessages() {
	Expects(_id);

	const auto result = tde2e_api::call_pull_outbound_messages(libId());
	if (!result.is_ok()) {
		LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		return;
	} else if (!result.value().empty()) {
		_outboundBlocks.fire(
			QByteArray::fromStdString(result.value().back()));
	}
}

void Call::apply(
		int subchain,
		int index,
		const Block &block,
		bool fromShortPoll) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);
	Expects(_id || !fromShortPoll || !subchain);

	if (!subchain && index >= _lastBlock0Height) {
		_lastBlock0 = block;
		_lastBlock0Height = index;
	}
	if (failed()) {
		return;
	}

	auto &entry = _subchains[subchain];
	if (!fromShortPoll) {
		entry.lastUpdate = crl::now();
		if (index > entry.height || (!_id && subchain != 0)) {
			entry.waiting.emplace(index, block);
			checkWaitingBlocks(subchain);
			return;
		}
	}

	if (failed()) {
		return;
	} else if (!_id || entry.height == index) {
		apply(subchain, block);
	}
	entry.height = index + 1;
	checkWaitingBlocks(subchain);
}

void Call::checkWaitingBlocks(int subchain, bool waited) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);

	if (failed()) {
		return;
	}

	auto &entry = _subchains[subchain];
	if (!_id) {
		entry.waitingTimer.callOnce(kShortPollChainBlocksWaitFor);
		return;
	} else if (entry.shortPolling) {
		return;
	}
	auto &waiting = entry.waiting;
	entry.shortPollTimer.cancel();
	while (!waiting.empty()) {
		const auto level = waiting.begin()->first;
		if (level > entry.height + 1) {
			if (waited) {
				shortPoll(subchain);
			} else {
				entry.waitingTimer.callOnce(kShortPollChainBlocksWaitFor);
			}
			return;
		} else if (level == entry.height + 1) {
			const auto slice = Slice(waiting.begin()->second.data);
			if (subchain) {
				auto result = tde2e_api::call_receive_inbound_message(
					libId(),
					slice);
				if (!result.is_ok()) {
					LOG_AND_FAIL(result.error(), CallFailure::Unknown);
					return;
				}
				_emojiHash = result.value().emoji_hash.has_value()
					? QByteArray::fromStdString(*result.value().emoji_hash)
					: QByteArray();
				checkForOutboundMessages();
			} else {
				const auto result = tde2e_api::call_apply_block(
					libId(),
					slice);
				if (!result.is_ok()) {
					LOG_AND_FAIL(result.error(), CallFailure::Unknown);
					return;
				}
				_participantsSet = ParseParticipantsSet(result.value());
			}
			entry.height = level;
		}
		waiting.erase(waiting.begin());
	}
	entry.waitingTimer.cancel();
	entry.shortPollTimer.callOnce(kShortPollChainBlocksTimeout);
}

void Call::shortPoll(int subchain) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);

	auto &entry = _subchains[subchain];
	entry.waitingTimer.cancel();
	entry.shortPollTimer.cancel();
	if (subchain && !_id) {
		// Not ready.
		entry.waitingTimer.callOnce(kShortPollChainBlocksWaitFor);
		return;
	}
	entry.shortPolling = true;
	_subchainRequests.fire({ subchain, entry.height });
}

std::int64_t Call::libId() const {
	return std::int64_t(_id.v);
}

rpl::producer<Call::SubchainRequest> Call::subchainRequests() const {
	return _subchainRequests.events();
}

void Call::subchainBlocksRequestFinished(int subchain) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);

	if (failed()) {
		return;
	}

	auto &entry = _subchains[subchain];
	entry.shortPolling = false;
	checkWaitingBlocks(subchain);
}

rpl::producer<QByteArray> Call::sendOutboundBlock() const {
	return _outboundBlocks.events();
}

std::optional<CallFailure> Call::failed() const {
	return _failure;
}

rpl::producer<CallFailure> Call::failures() const {
	if (_failure) {
		return rpl::single(*_failure);
	}
	return _failures.events();
}

QByteArray Call::emojiHash() const {
	return _emojiHash.current();
}

rpl::producer<QByteArray> Call::emojiHashValue() const {
	return _emojiHash.value();
}

std::vector<uint8_t> Call::encrypt(const std::vector<uint8_t> &data) const {
	const auto result = tde2e_api::call_encrypt(libId(), Slice(data));
	if (!result.is_ok()) {
		return {};
	}
	const auto &value = result.value();
	const auto start = reinterpret_cast<const uint8_t*>(value.data());
	const auto end = start + value.size();
	return std::vector<uint8_t>{ start, end };
}

std::vector<uint8_t> Call::decrypt(const std::vector<uint8_t> &data) const {
	const auto result = tde2e_api::call_decrypt(libId(), Slice(data));
	if (!result.is_ok()) {
		return {};
	}
	const auto &value = result.value();
	const auto start = reinterpret_cast<const uint8_t*>(value.data());
	const auto end = start + value.size();
	return std::vector<uint8_t>{ start, end };
}

} // namespace TdE2E
