/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/auth_key.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace MTP::details {

enum class DcKeyState {
	MaybeExisting,
	DefinitelyDestroyed,
};

class DcKeyChecker final {
public:
	DcKeyChecker(
		not_null<Instance*> instance,
		ShiftedDcId shiftedDcId,
		const AuthKeyPtr &persistentKey);

	[[nodiscard]] SecureRequest prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId);
	bool handleResponse(MTPlong requestMsgId, const mtpBuffer &response);

private:
	const not_null<Instance*> _instance;
	const ShiftedDcId _shiftedDcId = 0;
	const AuthKeyPtr _persistentKey;
	mtpMsgId _requestMsgId = 0;

};

} // namespace MTP::details
