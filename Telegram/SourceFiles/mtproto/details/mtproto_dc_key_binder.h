/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/mtproto_auth_key.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace MTP::details {

enum class DcKeyBindState {
	Unknown,
	Success,
	Failed,
	DefinitelyDestroyed,
};

class DcKeyBinder final {
public:
	DcKeyBinder(AuthKeyPtr &&persistentKey);

	[[nodiscard]] bool requested() const;
	[[nodiscard]] SecureRequest prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId);
	[[nodiscard]] DcKeyBindState handleResponse(
		MTPlong requestMsgId,
		const mtpBuffer &response);
	[[nodiscard]] AuthKeyPtr persistentKey() const;

	[[nodiscard]] static bool IsDestroyedTemporaryKeyError(
		const mtpBuffer &buffer);

private:
	AuthKeyPtr _persistentKey;
	mtpMsgId _requestMsgId = 0;

};

} // namespace MTP::details
