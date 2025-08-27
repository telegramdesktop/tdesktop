/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics_sender.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Api {

StatisticsRequestSender::StatisticsRequestSender(
	not_null<PeerData*> peer)
: _peer(peer)
, _channel(peer->asChannel())
, _user(peer->asUser())
, _api(&_peer->session().api().instance())
, _timer([=] { checkRequests(); }) {
}

MTP::Sender &StatisticsRequestSender::api() {
	return _api;
}

not_null<ChannelData*> StatisticsRequestSender::channel() {
	Expects(_channel);
	return _channel;
}

not_null<UserData*> StatisticsRequestSender::user() {
	Expects(_user);
	return _user;
}

void StatisticsRequestSender::checkRequests() {
	for (auto i = begin(_requests); i != end(_requests);) {
		for (auto j = begin(i->second); j != end(i->second);) {
			if (_api.pending(*j)) {
				++j;
			} else {
				_peer->session().api().unregisterStatsRequest(
					i->first,
					*j);
				j = i->second.erase(j);
			}
		}
		if (i->second.empty()) {
			i = _requests.erase(i);
		} else {
			++i;
		}
	}
	if (_requests.empty()) {
		_timer.cancel();
	}
}

auto StatisticsRequestSender::ensureRequestIsRegistered()
-> StatisticsRequestSender::Registered {
	const auto id = _api.allocateRequestId();
	const auto dcId = _peer->owner().statsDcId(_peer);
	if (dcId) {
		_peer->session().api().registerStatsRequest(dcId, id);
		_requests[dcId].emplace(id);
		if (!_timer.isActive()) {
			constexpr auto kCheckRequestsTimer = 10 * crl::time(1000);
			_timer.callEach(kCheckRequestsTimer);
		}
	}
	return StatisticsRequestSender::Registered{ id, dcId };
}

StatisticsRequestSender::~StatisticsRequestSender() {
	for (const auto &[dcId, ids] : _requests) {
		for (const auto id : ids) {
			_peer->session().api().unregisterStatsRequest(dcId, id);
		}
	}
}

} // namespace Api
