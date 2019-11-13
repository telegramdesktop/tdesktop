/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dc_key_checker.h"

#include "mtproto/mtp_instance.h"

#include <QtCore/QPointer>

namespace MTP::details {

DcKeyChecker::DcKeyChecker(
	not_null<Instance*> instance,
	DcId dcId,
	const AuthKeyPtr &key,
	FnMut<void()> destroyMe)
: _instance(instance)
, _dcId(dcId)
, _key(key)
, _destroyMe(std::move(destroyMe)) {
	crl::on_main(instance, [=] {
		auto destroy = std::move(_destroyMe);
		destroy();
	});
}

} // namespace MTP::details
