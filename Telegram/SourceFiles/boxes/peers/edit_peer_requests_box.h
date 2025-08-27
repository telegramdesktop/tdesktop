/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Ui {
class RippleAnimation;
} // namespace Ui

class RequestsBoxController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	RequestsBoxController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);
	~RequestsBoxController();

	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(
		not_null<PeerListRow*> row,
		int element) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	class RowHelper;

	struct SavedState : SavedStateBase {
		using SearchStateBase = PeerListSearchController::SavedStateBase;
		std::unique_ptr<SearchStateBase> searchState;
		base::flat_map<not_null<UserData*>, TimeId> dates;
		TimeId offsetDate = 0;
		UserData *offsetUser = nullptr;
		bool allLoaded = false;
		bool wasLoading = false;
	};

	static std::unique_ptr<PeerListSearchController> CreateSearchController(
		not_null<PeerData*> peer);

	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user,
		TimeId date = 0);

	void appendRow(not_null<UserData*> user, TimeId date);
	void refreshDescription();
	void processRequest(not_null<UserData*> user, bool approved);

	void subscribeToMigration();
	void migrate(not_null<ChatData*> chat, not_null<ChannelData*> channel);

	const not_null<Window::SessionNavigation*> _navigation;
	const std::unique_ptr<RowHelper> _helper;
	not_null<PeerData*> _peer;
	MTP::Sender _api;

	base::flat_map<not_null<UserData*>, TimeId> _dates;

	TimeId _offsetDate = 0;
	UserData *_offsetUser = nullptr;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

// Members, banned and restricted users server side search.
class RequestsBoxSearchController final : public PeerListSearchController {
public:
	RequestsBoxSearchController(not_null<PeerData*> peer);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

	void removeFromCache(not_null<UserData*> user);
	[[nodiscard]] TimeId dateForUser(not_null<UserData*> user);

	std::unique_ptr<SavedStateBase> saveState() const override;
	void restoreState(std::unique_ptr<SavedStateBase> state) override;

private:
	struct SavedState : SavedStateBase {
		QString query;
		TimeId offsetDate = 0;
		UserData *offsetUser = nullptr;
		bool allLoaded = false;
		bool wasLoading = false;
	};
	struct Item {
		not_null<UserData*> user;
		TimeId date = 0;
	};
	struct CacheEntry {
		std::vector<Item> items;
		int requestedCount = 0;
	};
	struct Query {
		QString text;
		TimeId offsetDate = 0;
		UserData *offsetUser = nullptr;
	};

	void searchOnServer();
	bool searchInCache();
	void searchDone(
		mtpRequestId requestId,
		const std::vector<Item> &items,
		int requestedCount);

	not_null<PeerData*> _peer;
	MTP::Sender _api;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	UserData *_offsetUser = nullptr;
	bool _allLoaded = false;
	base::flat_map<QString, CacheEntry> _cache;
	base::flat_map<mtpRequestId, Query> _queries;
	base::flat_map<not_null<UserData*>, TimeId> _dates;

};
