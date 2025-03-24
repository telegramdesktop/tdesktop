/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace TdE2E {

struct PrivateKeyId {
	uint64 v = 0;
};

struct PublicKey {
	uint64 a = 0;
	uint64 b = 0;
	uint64 c = 0;
	uint64 d = 0;
};

struct ParticipantState {
	uint64 id = 0;
	PublicKey key;
};

struct CallState {
	PrivateKeyId myKeyId;
	PublicKey myKey;
};

[[nodiscard]] CallState CreateCallState();

} // namespace TdE2E
