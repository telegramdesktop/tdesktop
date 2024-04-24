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
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kLimit = 64;
constexpr auto kRequestTimeLimit = 10 * crl::time(1000);

[[nodiscard]] float64 RatingDelta(TimeId now, TimeId was, int decay) {
	return std::exp((now - was) * 1. / decay);
}

[[nodiscard]] quint64 SerializeRating(float64 rating) {
	return quint64(
		base::SafeRound(std::clamp(rating, 0., 1'000'000.) * 1'000'000.));
}

[[nodiscard]] float64 DeserializeRating(quint64 rating) {
	return std::clamp(
		rating,
		quint64(),
		quint64(1'000'000'000'000ULL)
	) / 1'000'000.;
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
	_session->local().readSearchSuggestions();

	return _list
		| ranges::view::transform(&TopPeer::peer)
		| ranges::to_vector;
}

bool TopPeers::disabled() const {
	_session->local().readSearchSuggestions();

	return _disabled;
}

rpl::producer<> TopPeers::updates() const {
	return _updates.events();
}

void TopPeers::remove(not_null<PeerData*> peer) {
	const auto i = ranges::find(_list, peer, &TopPeer::peer);
	if (i != end(_list)) {
		_list.erase(i);
		updated();
	}

	_requestId = _session->api().request(MTPcontacts_ResetTopPeerRating(
		MTP_topPeerCategoryCorrespondents(),
		peer->input
	)).send();
}

void TopPeers::increment(not_null<PeerData*> peer, TimeId date) {
	_session->local().readSearchSuggestions();

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
			updated();
		} else {
			_session->local().writeSearchSuggestionsDelayed();
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
	_session->local().readSearchSuggestions();

	if (disabled) {
		if (!_disabled || !_list.empty()) {
			_disabled = true;
			_list.clear();
			updated();
		}
	} else if (_disabled) {
		_disabled = false;
		updated();
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
			updated();
		}, [&](const MTPDcontacts_topPeersDisabled &) {
			if (!_disabled) {
				_list.clear();
				_disabled = true;
				updated();
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
	for (const auto &top : _list | ranges::views::take(kLimit)) {
		HashUpdate(hash, peerToUser(top.peer->id).bare);
	}
	return HashFinalize(hash);
}

void TopPeers::updated() {
	_updates.fire({});
	_session->local().writeSearchSuggestionsDelayed();
}

QByteArray TopPeers::serialize() const {
	_session->local().readSearchSuggestions();

	if (!_disabled && _list.empty()) {
		return {};
	}
	auto size = 3 * sizeof(quint32); // AppVersion, disabled, count
	const auto count = std::min(int(_list.size()), kLimit);
	auto &&list = _list | ranges::views::take(count);
	for (const auto &top : list) {
		size += Serialize::peerSize(top.peer) + sizeof(quint64);
	}
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< quint32(AppVersion)
		<< quint32(_disabled ? 1 : 0)
		<< quint32(_list.size());
	for (const auto &top : list) {
		Serialize::writePeer(stream, top.peer);
		stream << SerializeRating(top.rating);
	}
	return std::move(stream).result();
}

void TopPeers::applyLocal(QByteArray serialized) {
	if (_lastReceived) {
		return;
	}
	_list.clear();
	_disabled = false;
	if (serialized.isEmpty()) {
		return;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto streamAppVersion = quint32();
	auto disabled = quint32();
	auto count = quint32();
	stream >> streamAppVersion >> disabled >> count;
	if (!stream.ok()) {
		return;
	}
	_list.reserve(count);
	for (auto i = 0; i != int(count); ++i) {
		auto rating = quint64();
		const auto peer = Serialize::readPeer(
			_session,
			streamAppVersion,
			stream);
		stream >> rating;
		if (stream.ok() && peer) {
			_list.push_back({
				.peer = peer,
				.rating = DeserializeRating(rating),
			});
		} else {
			_list.clear();
			return;
		}
	}
	_disabled = (disabled == 1);
}

} // namespace Data
