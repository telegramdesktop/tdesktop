/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {

bool MessagesSearchMerged::RequestCompare::operator()(
		const Request &a,
		const Request &b) const {
	return (a.query < b.query) && (a.from < b.from);
}

MessagesSearchMerged::MessagesSearchMerged(not_null<History*> history)
: _apiSearch(history)
, _migratedSearch(history->migrateFrom()
	? std::make_optional<MessagesSearch>(history->migrateFrom())
	: std::nullopt) {

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

void MessagesSearchMerged::addFound(const FoundMessages &data) {
	for (const auto &message : data.messages) {
		_concatedFound.messages.push_back(message);
	}
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _concatedFound;
}

void MessagesSearchMerged::clear() {
	_concatedFound = {};
	_migratedFirstFound = {};
}

void MessagesSearchMerged::search(const Request &search) {
	if (_migratedSearch) {
		_waitingForTotal = true;
		_migratedSearch->searchMessages(search.query, search.from);
	}
	_apiSearch.searchMessages(search.query, search.from);
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
