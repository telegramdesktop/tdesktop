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

CallState CreateCallState() {
	const auto id = tde2e_api::key_generate_temporary_private_key();
	Assert(id.is_ok());
	const auto key = tde2e_api::key_to_public_key(id.value());
	Assert(key.is_ok());

	auto result = CallState{
		.myKeyId = PrivateKeyId{ .v = uint64(id.value()) },
	};
	Assert(key.value().size() == sizeof(result.myKey));
	memcpy(&result.myKey, key.value().data(), 32);

	return result;
}

} // namespace TdE2E
