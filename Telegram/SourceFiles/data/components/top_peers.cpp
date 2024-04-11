/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/top_peers.h"

#include "api/api_hash.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"

namespace Data {
namespace {

constexpr auto kLimit = 32;
constexpr auto kRequestTimeLimit = 10 * crl::time(1000);

[[nodiscard]] float64 RatingDelta(TimeId now, TimeId was, int decay) {
	return std::exp((now - was) * 1. / decay);
}

} // namespace

TopPeers::TopPeers(not_null<Main::Session*> session)
: _session(session) {
	using namespace rpl::mappers;
	crl::on_main(session, [=] {
		_session->data().chatsListLoadedEvents(
		) | rpl::filter(_1 == nullptr) | rpl::start_with_next([=] {
			crl::on_main(_session, [=] {
				request();
			});
		}, _session->lifetime());
	});
}

TopPeers::~TopPeers() = default;

std::vector<not_null<PeerData*>> TopPeers::list() const {
	return _list
		| ranges::view::transform(&TopPeer::peer)
		| ranges::to_vector;
}

bool TopPeers::disabled() const {
	return _disabled;
}

rpl::producer<> TopPeers::updates() const {
	return _updates.events();
}

void TopPeers::remove(not_null<PeerData*> peer) {
	const auto i = ranges::find(_list, peer, &TopPeer::peer);
	if (i != end(_list)) {
		_list.erase(i);
		_updates.fire({});
	}

	_requestId = _session->api().request(MTPcontacts_ResetTopPeerRating(
		MTP_topPeerCategoryCorrespondents(),
		peer->input
	)).send();
}

void TopPeers::increment(not_null<PeerData*> peer, TimeId date) {
	if (_disabled || date <= _lastReceivedDate) {
		return;
	}
	if (const auto user = peer->asUser(); user && !user->isBot()) {
		auto changed = false;
		auto i = ranges::find(_list, peer, &TopPeer::peer);
		if (i == end(_list)) {
			_list.push_back({ .peer = peer });
			i = end(_list) - 1;
			changed = true;
		}
		const auto &config = peer->session().mtp().config();
		const auto decay = config.values().ratingDecay;
		i->rating += RatingDelta(date, _lastReceivedDate, decay);
		for (; i != begin(_list); --i) {
			if (i->rating >= (i - 1)->rating) {
				changed = true;
				std::swap(*i, *(i - 1));
			} else {
				break;
			}
		}
		if (changed) {
			_updates.fire({});
		}
	}
}

void TopPeers::reload() {
	if (_requestId
		|| (_lastReceived
			&& _lastReceived + kRequestTimeLimit > crl::now())) {
		return;
	}
	request();
}

void TopPeers::toggleDisabled(bool disabled) {
	if (disabled) {
		if (!_disabled || !_list.empty()) {
			_disabled = true;
			_list.clear();
			_updates.fire({});
		}
	} else if (_disabled) {
		_disabled = false;
		_updates.fire({});
	}

	_session->api().request(MTPcontacts_ToggleTopPeers(
		MTP_bool(!disabled)
	)).done([=] {
		if (!_disabled) {
			request();
		}
	}).send();
}

void TopPeers::request() {
	if (_requestId) {
		return;
	}

	_requestId = _session->api().request(MTPcontacts_GetTopPeers(
		MTP_flags(MTPcontacts_GetTopPeers::Flag::f_correspondents),
		MTP_int(0),
		MTP_int(kLimit),
		MTP_long(countHash())
	)).done([=](const MTPcontacts_TopPeers &result, const MTP::Response &response) {
		_lastReceivedDate = TimeId(response.outerMsgId >> 32);
		_lastReceived = crl::now();
		_requestId = 0;

		result.match([&](const MTPDcontacts_topPeers &data) {
			_disabled = false;
			const auto owner = &_session->data();
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			for (const auto &category : data.vcategories().v) {
				const auto &data = category.data();
				data.vcategory().match(
					[&](const MTPDtopPeerCategoryCorrespondents &) {
					_list = ranges::views::all(
						data.vpeers().v
					) | ranges::views::transform([&](const MTPTopPeer &top) {
						return TopPeer{
							owner->peer(peerFromMTP(top.data().vpeer())),
							top.data().vrating().v,
						};
					}) | ranges::to_vector;
				}, [](const auto &) {
					LOG(("API Error: Unexpected top peer category."));
				});
			}
			_updates.fire({});
		}, [&](const MTPDcontacts_topPeersDisabled &) {
			if (!_disabled) {
				_list.clear();
				_disabled = true;
				_updates.fire({});
			}
		}, [](const MTPDcontacts_topPeersNotModified &) {
		});
	}).fail([=] {
		_lastReceived = crl::now();
		_requestId = 0;
	}).send();
}

uint64 TopPeers::countHash() const {
	using namespace Api;
	auto hash = HashInit();
	for (const auto &top : _list) {
		HashUpdate(hash, peerToUser(top.peer->id).bare);
	}
	return HashFinalize(hash);
}

} // namespace Data
