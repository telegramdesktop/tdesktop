/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "dialogs/ui/posts_search_intro.h"
#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Dialogs {

struct PostsSearchState {
	std::optional<PostsSearchIntroState> intro;
	std::vector<not_null<HistoryItem*>> page;
	int totalCount = 0;
	bool loading = false;
};

class PostsSearch final {
public:
	explicit PostsSearch(not_null<Main::Session*> session);

	[[nodiscard]] rpl::producer<PostsSearchState> stateUpdates() const;
	[[nodiscard]] rpl::producer<PostsSearchState> pagesUpdates() const;

	void setQuery(const QString &query);
	int setAllowedStars(int stars);
	void requestMore();

private:
	struct Entry {
		std::vector<std::vector<not_null<HistoryItem*>>> pages;
		int totalCount = 0;
		mtpRequestId searchId = 0;
		mtpRequestId checkId = 0;
		PeerData *offsetPeer = nullptr;
		MsgId offsetId = 0;
		int offsetRate = 0;
		int allowedStars = 0;
		mutable int pagesPushed = 0;
		bool loaded = false;
	};

	void recheck();
	void applyQuery();
	void requestSearch(const QString &query);
	void requestState(const QString &query, bool force = false);
	void setFloodStateFrom(const MTPDsearchPostsFlood &data);
	void pushStateUpdate(const Entry &entry);
	void maybePushPremiumUpdate();

	const not_null<Main::Session*> _session;

	MTP::Sender _api;

	base::Timer _timer;
	base::Timer _recheckTimer;
	base::flat_map<QString, Entry> _entries;
	std::optional<QString> _queryExact;
	std::optional<QString> _query;
	QString _queryPushed;

	std::optional<PostsSearchIntroState> _floodState;

	rpl::event_stream<PostsSearchState> _stateUpdates;
	rpl::event_stream<PostsSearchState> _pagesUpdates;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs
