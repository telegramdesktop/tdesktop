/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tde2e/tde2e_integration.h"

#include "data/data_user.h"
#include "tde2e/tde2e_api.h"

namespace TdE2E {

UserId MakeUserId(not_null<UserData*> user) {
	return MakeUserId(peerToUser(user->id));
}

UserId MakeUserId(::UserId id) {
	return { .v = id.bare };
}

MTPint256 PublicKeyToMTP(const PublicKey &key) {
	return MTP_int256(MTP_int128(key.a, key.b), MTP_int128(key.c, key.d));
}

} // namespace TdE2E
