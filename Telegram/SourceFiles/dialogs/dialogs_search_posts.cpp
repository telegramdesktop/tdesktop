/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_posts.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Dialogs {
namespace {

constexpr auto kQueryDelay = crl::time(500);
constexpr auto kPerPage = 50;

[[nodiscard]] const QRegularExpression &SearchSplitter() {
	static const auto result = QRegularExpression(QString::fromLatin1(""
		"[\\s\\-\\+\\(\\)\\[\\]\\{\\}\\<\\>\\,\\.\\!\\_\\;\\\"\\'\\x0]"));
	return result;
}

} // namespace

PostsSearch::PostsSearch(not_null<Main::Session*> session)
: _session(session)
, _api(&_session->api().instance())
, _timer([=] { applyQuery(); })
, _recheckTimer([=] { recheck(); }) {
	Data::AmPremiumValue(_session) | rpl::start_with_next([=] {
		maybePushPremiumUpdate();
	}, _lifetime);
}

rpl::producer<PostsSearchState> PostsSearch::stateUpdates() const {
	return _stateUpdates.events();
}

rpl::producer<PostsSearchState> PostsSearch::pagesUpdates() const {
	return _pagesUpdates.events();
}

void PostsSearch::requestMore() {
	if (!_query) {
		return;
	}
	auto &entry = _entries[*_query];
	if (_queryPushed != *_query || !entry.pagesPushed) {
		return;
	} else if (entry.pagesPushed < entry.pages.size()) {
		_pagesUpdates.fire(PostsSearchState{
			.page = entry.pages[entry.pagesPushed++],
			.totalCount = entry.totalCount,
		});
	} else {
		requestSearch(*_query);
	}
}

void PostsSearch::setQuery(const QString &query) {
	const auto words = TextUtilities::PrepareSearchWords(
		query,
		&SearchSplitter());
	const auto prepared = words.isEmpty() ? QString() : words.join(' ');
	if (_queryExact == query) {
		return;
	}
	_queryExact = query;
	_query = prepared;
	const auto i = _entries.find(prepared);
	if (i != end(_entries)) {
		pushStateUpdate(i->second);
	} else if (prepared.isEmpty()) {
		applyQuery();
	} else {
		_timer.callOnce(kQueryDelay);
	}
}

int PostsSearch::setAllowedStars(int stars) {
	if (!_query) {
		return 0;
	} else if (_floodState) {
		if (_floodState->freeSearchesLeft > 0) {
			stars = 0;
		} else if (_floodState->nextFreeSearchTime > 0
			&& _floodState->nextFreeSearchTime <= base::unixtime::now()) {
			stars = 0;
		} else {
			stars = std::min(int(_floodState->starsPerPaidSearch), stars);
		}
	}
	_entries[*_query].allowedStars = stars;
	requestSearch(*_query);
	return stars;
}

void PostsSearch::pushStateUpdate(const Entry &entry) {
	Expects(_query.has_value());

	const auto initial = (_queryPushed != *_query);
	if (initial) {
		_queryPushed = *_query;
		entry.pagesPushed = 0;
	} else if (entry.pagesPushed > 0) {
		if (entry.pagesPushed < entry.pages.size()) {
			_pagesUpdates.fire(PostsSearchState{
				.page = entry.pages[entry.pagesPushed++],
				.totalCount = entry.totalCount,
			});
		}
		return;
	}
	const auto empty = entry.pages.empty()
		|| (entry.pages.size() == 1 && entry.pages.front().empty());
	if (!empty || (entry.loaded && !_query->isEmpty())) {
		if (!entry.pages.empty()) {
			++entry.pagesPushed;
		}
		_stateUpdates.fire(PostsSearchState{
			.page = (entry.pages.empty()
				? std::vector<not_null<HistoryItem*>>()
				: entry.pages.front()),
			.totalCount = entry.totalCount,
		});
	} else if (entry.checkId || entry.searchId) {
		_stateUpdates.fire(PostsSearchState{
			.loading = true,
		});
	} else {
		Assert(_floodState.has_value());
		auto copy = _floodState;
		copy->query = *_queryExact;
		copy->needsPremium = !_session->premium();
		_stateUpdates.fire(PostsSearchState{
			.intro = std::move(copy),
		});
	}
}

void PostsSearch::maybePushPremiumUpdate() {
	if (!_floodState || !_query) {
		return;
	}
	auto &entry = _entries[*_query];
	if (!entry.pages.empty()
		|| entry.loaded
		|| entry.checkId
		|| entry.searchId) {
		return;
	}
	pushStateUpdate(entry);
}

void PostsSearch::applyQuery() {
	Expects(_query.has_value());

	_timer.cancel();
	if (_query->isEmpty()) {
		requestSearch(QString());
	} else {
		requestState(*_query);
	}
}

void PostsSearch::requestSearch(const QString &query) {
	auto &entry = _entries[query];
	if (entry.searchId || entry.loaded) {
		return;
	}

	const auto useStars = entry.allowedStars;
	entry.allowedStars = 0;

	using Flag = MTPchannels_SearchPosts::Flag;
	entry.searchId = _api.request(MTPchannels_SearchPosts(
		MTP_flags(Flag::f_query
			| (useStars ? Flag::f_allow_paid_stars : Flag())),
		MTP_string(), // hashtag
		MTP_string(query),
		MTP_int(entry.offsetRate),
		(entry.offsetPeer ? entry.offsetPeer->input : MTP_inputPeerEmpty()),
		MTP_int(entry.offsetId),
		MTP_int(kPerPage),
		MTP_long(useStars)
	)).done([=](const MTPmessages_Messages &result) {
		auto &entry = _entries[query];
		entry.searchId = 0;

		const auto initial = !entry.offsetId;
		const auto owner = &_session->data();
		const auto processList = [&](const MTPVector<MTPMessage> &messages) {
			auto result = std::vector<not_null<HistoryItem*>>();
			for (const auto &message : messages.v) {
				const auto msgId = IdFromMessage(message);
				const auto peerId = PeerFromMessage(message);
				const auto lastDate = DateFromMessage(message);
				if (const auto peer = owner->peerLoaded(peerId)) {
					if (lastDate) {
						const auto item = owner->addNewMessage(
							message,
							MessageFlags(),
							NewMessageType::Existing);
						result.push_back(item);
					}
					entry.offsetPeer = peer;
				} else {
					LOG(("API Error: a search results with not loaded peer %1"
						).arg(peerId.value));
				}
				entry.offsetId = msgId;
			}
			return result;
		};
		auto totalCount = 0;
		auto messages = result.match([&](const MTPDmessages_messages &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			entry.loaded = true;
			auto list = processList(data.vmessages());
			totalCount = list.size();
			return list;
		}, [&](const MTPDmessages_messagesSlice &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			auto list = processList(data.vmessages());
			const auto nextRate = data.vnext_rate();
			const auto rateUpdated = nextRate
				&& (nextRate->v != entry.offsetRate);
			const auto finished = list.empty();
			if (rateUpdated) {
				entry.offsetRate = nextRate->v;
			}
			if (finished) {
				entry.loaded = true;
			}
			totalCount = data.vcount().v;
			if (const auto flood = data.vsearch_flood()) {
				setFloodStateFrom(flood->data());
			}
			return list;
		}, [&](const MTPDmessages_channelMessages &data) {
			LOG(("API Error: "
				"received messages.channelMessages when no channel "
				"was passed! (PostsSearch::performSearch)"));
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			auto list = processList(data.vmessages());
			if (list.empty()) {
				entry.loaded = true;
			}
			totalCount = data.vcount().v;
			return list;
		}, [&](const MTPDmessages_messagesNotModified &) {
			LOG(("API Error: received messages.messagesNotModified! "
				"(PostsSearch::performSearch)"));
			entry.loaded = true;
			return std::vector<not_null<HistoryItem*>>();
		});
		if (initial) {
			entry.pages.clear();
		}
		entry.pages.push_back(std::move(messages));
		const auto count = int(ranges::accumulate(
			entry.pages,
			size_type(),
			ranges::plus(),
			&std::vector<not_null<HistoryItem*>>::size));
		const auto full = entry.loaded ? count : std::max(count, totalCount);
		entry.totalCount = full;
		if (_query == query) {
			pushStateUpdate(entry);
		}
	}).fail([=](const MTP::Error &error) {
		auto &entry = _entries[query];
		entry.searchId = 0;

		const auto initial = !entry.offsetId;
		const auto &type = error.type();
		if (initial && type.startsWith(u"FLOOD_WAIT_"_q)) {
			requestState(query);
		} else {
			entry.loaded = true;
		}
	}).handleFloodErrors().send();
}

void PostsSearch::setFloodStateFrom(const MTPDsearchPostsFlood &data) {
	_recheckTimer.cancel();
	const auto left = std::max(data.vremains().v, 0);
	const auto next = data.vwait_till().value_or_empty();
	if (!left && next > 0) {
		const auto now = base::unixtime::now();
		const auto delay = std::clamp(next - now, 1, 86401);
		_recheckTimer.callOnce(delay * crl::time(1000));
	}
	_floodState = PostsSearchIntroState{
		.freeSearchesPerDay = data.vtotal_daily().v,
		.freeSearchesLeft = left,
		.nextFreeSearchTime = next,
		.starsPerPaidSearch = uint32(data.vstars_amount().v),
	};
}

void PostsSearch::recheck() {
	requestState(*_query, true);
}

void PostsSearch::requestState(const QString &query, bool force) {
	auto &entry = _entries[query];
	if (force) {
		_api.request(base::take(entry.checkId)).cancel();
	} else if (entry.checkId || entry.loaded) {
		return;
	}

	using Flag = MTPchannels_CheckSearchPostsFlood::Flag;
	entry.checkId = _api.request(MTPchannels_CheckSearchPostsFlood(
		MTP_flags(Flag::f_query),
		MTP_string(query)
	)).done([=](const MTPSearchPostsFlood &result) {
		auto &entry = _entries[query];
		entry.checkId = 0;

		const auto &data = result.data();
		setFloodStateFrom(data);
		if (data.is_query_is_free()) {
			if (!entry.loaded) {
				requestSearch(query);
			}
		} else if (_query == query) {
			pushStateUpdate(entry);
		}
	}).fail([=](const MTP::Error &error) {
		auto &entry = _entries[query];
		entry.checkId = 0;
		entry.loaded = true;
	}).handleFloodErrors().send();
}

} // namespace Dialogs
