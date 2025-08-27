/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {

MessagesSearchMerged::MessagesSearchMerged(not_null<History*> history)
: _apiSearch(history) {
	if (const auto migrated = history->migrateFrom()) {
		_migratedSearch.emplace(migrated);
	}
	const auto checkWaitingForTotal = [=] {
		if (_waitingForTotal) {
			if (_concatedFound.total >= 0 && _migratedFirstFound.total >= 0) {
				_waitingForTotal = false;
				_concatedFound.total += _migratedFirstFound.total;
				_newFounds.fire({});
			}
		} else {
			_newFounds.fire({});
		}
	};

	const auto checkFull = [=](const FoundMessages &data) {
		if (data.total == int(_concatedFound.messages.size())) {
			_isFull = true;
			addFound(_migratedFirstFound);
		}
	};

	_apiSearch.messagesFounds(
	) | rpl::start_with_next([=](const FoundMessages &data) {
		if (data.nextToken == _concatedFound.nextToken) {
			addFound(data);
			checkFull(data);
			_nextFounds.fire({});
		} else {
			_concatedFound = data;
			checkFull(data);
			checkWaitingForTotal();
		}
	}, _lifetime);

	if (_migratedSearch) {
		_migratedSearch->messagesFounds(
		) | rpl::start_with_next([=](const FoundMessages &data) {
			if (_isFull) {
				addFound(data);
			}
			if (data.nextToken == _migratedFirstFound.nextToken) {
				_nextFounds.fire({});
			} else {
				_migratedFirstFound = data;
				checkWaitingForTotal();
			}
		}, _lifetime);
	}
}

void MessagesSearchMerged::disableMigrated() {
	_migratedSearch = std::nullopt;
}

void MessagesSearchMerged::addFound(const FoundMessages &data) {
	for (const auto &message : data.messages) {
		_concatedFound.messages.push_back(message);
	}
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _concatedFound;
}

const MessagesSearch::Request &MessagesSearchMerged::request() const {
	return _request;
}

void MessagesSearchMerged::clear() {
	_concatedFound = {};
	_migratedFirstFound = {};
}

void MessagesSearchMerged::search(const Request &search) {
	_request = search;
	if (_migratedSearch) {
		_waitingForTotal = true;
		_migratedSearch->searchMessages(search);
	}
	_apiSearch.searchMessages(search);
}

void MessagesSearchMerged::searchMore() {
	if (_migratedSearch && _isFull) {
		_migratedSearch->searchMore();
	} else {
		_apiSearch.searchMore();
	}
}

rpl::producer<> MessagesSearchMerged::newFounds() const {
	return _newFounds.events();
}

rpl::producer<> MessagesSearchMerged::nextFounds() const {
	return _nextFounds.events();
}

} // namespace Api
