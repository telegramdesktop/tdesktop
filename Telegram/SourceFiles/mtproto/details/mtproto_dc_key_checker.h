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

class DcKeyChecker final {
public:
	DcKeyChecker(
		not_null<Instance*> instance,
		DcId dcId,
		const AuthKeyPtr &key,
		FnMut<void()> destroyMe);

private:
	not_null<Instance*> _instance;
	DcId _dcId = 0;
	AuthKeyPtr _key;
	FnMut<void()> _destroyMe;

};

} // namespace MTP::details
