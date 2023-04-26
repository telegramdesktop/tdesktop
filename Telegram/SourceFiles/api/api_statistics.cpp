/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "main/main_session.h"

namespace Api {
namespace {
} // namespace

Statistics::Statistics(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

rpl::producer<> Statistics::request(
		not_null<PeerData*> peer) const {
	return rpl::never<>();
}

} // namespace Api
