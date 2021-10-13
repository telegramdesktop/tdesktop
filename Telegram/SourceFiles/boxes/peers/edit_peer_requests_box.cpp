/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_requests_box.h"

#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"

namespace {

constexpr auto kFirstPageCount = 16;
constexpr auto kPerPage = 200;
constexpr auto kServerSearchDelay = crl::time(1000);

class Row final : public PeerListRow {
public:
	Row(not_null<UserData*> user, TimeId date);

	//QSize actionSize() const override;
	//void paintAction(
	//	Painter &p,
	//	int x,
	//	int y,
	//	int outerWidth,
	//	bool selected,
	//	bool actionSelected) override;

	//not_null<UserData*> user() const;

private:
	TimeId _date = 0;

};

Row::Row(not_null<UserData*> user, TimeId date)
: PeerListRow(user)
, _date(date) {
	setCustomStatus(QString::number(_date));
}

} // namespace

RequestsBoxController::RequestsBoxController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: PeerListController(CreateSearchController(peer))
, _navigation(navigation)
, _peer(peer)
, _api(&_peer->session().mtp()) {
	subscribeToMigration();
}

Main::Session &RequestsBoxController::session() const {
	return _peer->session();
}

auto RequestsBoxController::CreateSearchController(not_null<PeerData*> peer)
-> std::unique_ptr<PeerListSearchController> {
	return std::make_unique<RequestsBoxSearchController>(peer);
}

std::unique_ptr<PeerListRow> RequestsBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void RequestsBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(_peer->isBroadcast()
		? tr::lng_manage_peer_requests_channel()
		: tr::lng_manage_peer_requests());
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
	loadMoreRows();
}

void RequestsBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (_loadRequestId || _allLoaded) {
		return;
	}

	// First query is small and fast, next loads a lot of rows.
	const auto limit = _offsetDate ? kPerPage : kFirstPageCount;
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	_loadRequestId = _api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(Flag::f_requested),
		_peer->input,
		MTPstring(), // link
		MTPstring(), // q
		MTP_int(_offsetDate),
		_offsetUser ? _offsetUser->inputUser : MTP_inputUserEmpty(),
		MTP_int(limit)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		const auto firstLoad = !_offsetDate;
		_loadRequestId = 0;

		result.match([&](const MTPDmessages_chatInviteImporters &data) {
			const auto count = data.vcount().v;
			const auto &importers = data.vimporters().v;
			auto &owner = _peer->owner();
			for (const auto &importer : importers) {
				importer.match([&](const MTPDchatInviteImporter &data) {
					_offsetDate = data.vdate().v;
					_offsetUser = owner.user(data.vuser_id());
					appendRow(_offsetUser, _offsetDate);
				});
			}
			// To be sure - wait for a whole empty result list.
			_allLoaded = importers.isEmpty();
		});

		if (_allLoaded
			|| (firstLoad && delegate()->peerListFullRowsCount() > 0)) {
			refreshDescription();
		}
		delegate()->peerListRefreshRows();
	}).fail([=](const MTP::Error &error) {
		_loadRequestId = 0;
		_allLoaded = true;
	}).send();
}

void RequestsBoxController::refreshDescription() {
	setDescriptionText((delegate()->peerListFullRowsCount() > 0)
		? QString()
		: _peer->isBroadcast()
		? tr::lng_group_removed_list_about(tr::now)
		: tr::lng_channel_removed_list_about(tr::now));
}

void RequestsBoxController::rowClicked(not_null<PeerListRow*> row) {
	_navigation->showPeerInfo(row->peer());
}

void RequestsBoxController::appendRow(
		not_null<UserData*> user,
		TimeId date) {
	if (!delegate()->peerListFindRow(user->id.value)) {
		if (auto row = createRow(user, date)) {
			delegate()->peerListAppendRow(std::move(row));
			setDescriptionText(QString());
		}
	}
}

std::unique_ptr<PeerListRow> RequestsBoxController::createRow(
		not_null<UserData*> user,
		TimeId date) {
	if (!date) {
		const auto search = static_cast<RequestsBoxSearchController*>(
			searchController());
		date = search->dateForUser(user);
	}
	return std::make_unique<Row>(user, date);
}

void RequestsBoxController::subscribeToMigration() {
	const auto chat = _peer->asChat();
	if (!chat) {
		return;
	}
	SubscribeToMigration(
		chat,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(chat, channel); });
}

void RequestsBoxController::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
}

RequestsBoxSearchController::RequestsBoxSearchController(
	not_null<PeerData*> peer)
: _peer(peer)
, _api(&_peer->session().mtp()) {
	_timer.setCallback([=] { searchOnServer(); });
}

void RequestsBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offsetDate = 0;
		_offsetUser = nullptr;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(kServerSearchDelay);
		} else {
			_timer.cancel();
		}
	}
}

void RequestsBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());

	loadMoreRows();
}

bool RequestsBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

void RequestsBoxSearchController::removeFromCache(not_null<UserData*> user) {
	for (auto &entry : _cache) {
		auto &items = entry.second.items;
		const auto j = ranges::remove(items, user, &Item::user);
		if (j != end(items)) {
			entry.second.requestedCount -= (end(items) - j);
			items.erase(j, end(items));
		}
	}
}

TimeId RequestsBoxSearchController::dateForUser(not_null<UserData*> user) {
	if (const auto i = _dates.find(user); i != end(_dates)) {
		return i->second;
	}
	return {};
}

bool RequestsBoxSearchController::searchInCache() {
	const auto i = _cache.find(_query);
	if (i != _cache.cend()) {
		_requestId = 0;
		searchDone(
			_requestId,
			i->second.items,
			i->second.requestedCount);
		return true;
	}
	return false;
}

bool RequestsBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	} else if (_allLoaded || isLoading()) {
		return true;
	}
	// For search we request a lot of rows from the first query.
	// (because we've waited for search request by timer already,
	// so we don't expect it to be fast, but we want to fill cache).
	const auto limit = kPerPage;
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	_requestId = _api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(Flag::f_requested | Flag::f_q),
		_peer->input,
		MTPstring(), // link
		MTP_string(_query),
		MTP_int(_offsetDate),
		_offsetUser ? _offsetUser->inputUser : MTP_inputUserEmpty(),
		MTP_int(limit)
	)).done([=](
			const MTPmessages_ChatInviteImporters &result,
			mtpRequestId requestId) {
		auto items = std::vector<Item>();
		result.match([&](const MTPDmessages_chatInviteImporters &data) {
			const auto count = data.vcount().v;
			const auto &importers = data.vimporters().v;
			auto &owner = _peer->owner();
			items.reserve(importers.size());
			for (const auto &importer : importers) {
				importer.match([&](const MTPDchatInviteImporter &data) {
					items.push_back({
						owner.user(data.vuser_id()),
						data.vdate().v,
					});
				});
			}
		});
		searchDone(requestId, items, limit);

		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			const auto &query = it->second.text;
			if (it->second.offsetDate == 0) {
				auto &entry = _cache[query];
				entry.items = std::move(items);
				entry.requestedCount = limit;
			}
			_queries.erase(it);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_allLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();

	auto entry = Query();
	entry.text = _query;
	entry.offsetDate = _offsetDate;
	_queries.emplace(_requestId, entry);
	return true;
}

void RequestsBoxSearchController::searchDone(
		mtpRequestId requestId,
		const std::vector<Item> &items,
		int requestedCount) {
	if (_requestId != requestId) {
		return;
	}

	_requestId = 0;
	if (!_offsetDate) {
		_dates.clear();
	}
	for (const auto &[user, date] : items) {
		_offsetDate = date;
		_offsetUser = user;
		_dates.emplace(user, date);
		delegate()->peerListSearchAddRow(user);
	}
	if (items.size() < requestedCount) {
		// We want cache to have full information about a query with
		// small results count (that we don't need the second request).
		// So we don't wait for empty list unlike the non-search case.
		_allLoaded = true;
	}
	delegate()->peerListSearchRefreshRows();
}
