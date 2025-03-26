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

void Call::fail(CallFailure reason) {
	_failure = reason;
	_failures.fire_copy(reason);
}

PublicKey Call::myKey() const {
	return _myKey;
}

Block Call::makeZeroBlock() const {
	const auto publicKeyView = std::string_view{
		reinterpret_cast<const char*>(&_myKey),
		sizeof(_myKey),
	};
	const auto publicKeyId = tde2e_api::key_from_public_key(publicKeyView);
	Assert(publicKeyId.is_ok());

	const auto myKeyId = std::int64_t(_myKeyId.v);
	const auto result = tde2e_api::call_create_zero_block(myKeyId, {
		.height = 0,
		.participants = { {
		  .user_id = std::int64_t(_myUserId.v),
		  .public_key_id = publicKeyId.value(),
		  .permissions = kPermissionAdd | kPermissionRemove,
		} },
	});
	Assert(result.is_ok());

	return {
		.data = QByteArray::fromStdString(result.value()),
	};
}

void Call::create(const Block &last) {
	const auto id = tde2e_api::call_create(
		std::int64_t(_myKeyId.v),
		Slice(last.data));
	if (!id.is_ok()) {
		LOG_AND_FAIL(id.error(), CallFailure::Unknown);
		return;
	}

	for (auto i = 0; i != kSubChainsCount; ++i) {
		auto &entry = _subchains[i];
		entry.waitingTimer.setCallback([=] {
			checkWaitingBlocks(i, true);
		});
		entry.shortPollTimer.setCallback([=] {
			shortPoll(i);
		});
		entry.shortPollTimer.callOnce(kShortPollChainBlocksTimeout);
	}
}

void Call::apply(
		int subchain,
		int index,
		const Block &block,
		bool fromShortPoll) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);

	if (!subchain && !_id.v) {
		create(block);
	}
	if (failed()) {
		return;
	}

	auto &entry = _subchains[subchain];
	if (!fromShortPoll) {
		entry.lastUpdate = crl::now();
		if (index > entry.height + 1) {
			entry.waiting.emplace(index, block);
			checkWaitingBlocks(subchain);
			return;
		}
	}

	const auto result = tde2e_api::call_apply_block(
		std::int64_t(_id.v),
		Slice(block.data));
	if (!result.is_ok()) {
		LOG_AND_FAIL(result.error(), CallFailure::Unknown);
		return;
	}

	entry.height = std::max(entry.height, index);
	checkWaitingBlocks(subchain);
}

void Call::checkWaitingBlocks(int subchain, bool waited) {
	Expects(subchain >= 0 && subchain < kSubChainsCount);

	if (failed()) {
		return;
	}

	auto &entry = _subchains[subchain];
	if (entry.shortPolling) {
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
			const auto result = tde2e_api::call_apply_block(
				std::int64_t(_id.v),
				Slice(waiting.begin()->second.data));
			if (!result.is_ok()) {
				LOG_AND_FAIL(result.error(), CallFailure::Unknown);
				return;
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
	entry.shortPolling = true;
	_subchainRequests.fire({ subchain, entry.height });
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

std::optional<CallFailure> Call::failed() const {
	return _failure;
}

rpl::producer<CallFailure> Call::failures() const {
	if (_failure) {
		return rpl::single(*_failure);
	}
	return _failures.events();
}

std::vector<uint8_t> Call::encrypt(const std::vector<uint8_t> &data) const {
	const auto result = tde2e_api::call_encrypt(
		std::int64_t(_id.v),
		std::string_view{
			reinterpret_cast<const char*>(data.data()),
			data.size(),
		});
	if (!result.is_ok()) {
		return {};
	}
	const auto &value = result.value();
	const auto start = reinterpret_cast<const uint8_t*>(value.data());
	const auto end = start + value.size();
	return std::vector<uint8_t>{ start, end };
}

std::vector<uint8_t> Call::decrypt(const std::vector<uint8_t> &data) const {
	const auto result = tde2e_api::call_decrypt(
		std::int64_t(_id.v),
		std::string_view{
			reinterpret_cast<const char*>(data.data()),
			data.size(),
		});
	if (!result.is_ok()) {
		return {};
	}
	const auto &value = result.value();
	const auto start = reinterpret_cast<const uint8_t*>(value.data());
	const auto end = start + value.size();
	return std::vector<uint8_t>{ start, end };
}

} // namespace TdE2E
