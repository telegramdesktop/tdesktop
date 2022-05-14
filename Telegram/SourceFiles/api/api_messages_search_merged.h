/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_messages_search.h"

class History;
class PeerData;

namespace Api {

// Search in both of history and migrated history, if it exists.
class MessagesSearchMerged final {
public:
	struct Request {
		QString query;
		PeerData *from = nullptr;
	};
	struct RequestCompare {
		bool operator()(const Request &a, const Request &b) const;
	};
	using CachedRequests = std::set<Request, RequestCompare>;

	MessagesSearchMerged(not_null<History*> history);

	void clear();
	void search(const Request &search);
	void searchMore();

	[[nodiscard]] const FoundMessages &messages() const;

	[[nodiscard]] rpl::producer<> newFounds() const;
	[[nodiscard]] rpl::producer<> nextFounds() const;

private:
	void addFound(const FoundMessages &data);

	MessagesSearch _apiSearch;

	std::optional<MessagesSearch> _migratedSearch;
	FoundMessages _migratedFirstFound;

	FoundMessages _concatedFound;

	bool _waitingForTotal = false;
	bool _isFull = false;

	rpl::event_stream<> _newFounds;
	rpl::event_stream<> _nextFounds;

	rpl::lifetime _lifetime;

};

} // namespace Api
