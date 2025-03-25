/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tde2e/tde2e_api.h"

#include "base/assertion.h"

#include <tde2e/td/e2e/e2e_api.h>

namespace TdE2E {
namespace {

constexpr auto kPermissionAdd = 1;
constexpr auto kPermissionRemove = 2;

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
	tde2e_api::call_create(std::int64_t(_myKeyId.v), Slice(last.data));
}

Call::ApplyResult Call::apply(const Block &block) {
	const auto result = tde2e_api::call_apply_block(
		std::int64_t(_id.v),
		Slice(block.data));

	if (!result.is_ok()) {
		const auto error = result.error();
		(void)error;
	}

	return result.is_ok()
		? ApplyResult::Success
		: ApplyResult::BlockSkipped;
}

} // namespace TdE2E
