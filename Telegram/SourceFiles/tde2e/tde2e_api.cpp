/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tde2e/tde2e_api.h"

#include "base/algorithm.h"

#include <tde2e/td/e2e/e2e_api.h>

namespace TdE2E {

QByteArray GeneratePrivateKey() {
	const auto result = tde2e_api::key_generate_temporary_private_key();

	return {};
}

} // namespace TdE2E
