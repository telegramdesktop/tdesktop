/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/mtproto_auth_key.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace MTP::details {

class SerializedRequest;

enum class DcKeyBindState {
	Success,
	Failed,
	DefinitelyDestroyed,
};

class DcKeyBinder final {
public:
	explicit DcKeyBinder(AuthKeyPtr &&persistentKey);

	[[nodiscard]] SerializedRequest prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId);
	[[nodiscard]] DcKeyBindState handleResponse(const mtpBuffer &response);
	[[nodiscard]] AuthKeyPtr persistentKey() const;

private:
	AuthKeyPtr _persistentKey;

};

} // namespace MTP::details
