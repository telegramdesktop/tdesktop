/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "base/flat_set.h"
#include "base/weak_ptr.h"
#include "base/timer.h"
#include "mtproto/sender.h"

class History;

namespace style {
struct PeerListItem;
} // namespace style

namespace Data {
class Thread;
class Forum;
class ForumTopic;
} // namespace Data

namespace Ui {
struct OutlineSegment;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

[[nodiscard]] object_ptr<Ui::BoxContent> PrepareContactsBox(
	not_null<Window::SessionController*> sessionController);
[[nodiscard]] QBrush PeerListStoriesGradient(const style::PeerList &st);
[[nodiscard]] std::vector<Ui::OutlineSegment> PeerListStoriesSegments(
	int count,
	int unread,
	const QBrush &unreadBrush);

class PeerListRowWithLink : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setActionLink(const QString &action);

	void lazyInitialize(const style::PeerListItem &st) override;

protected:
	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	void refreshActionLink();

	QString _action;
	int _actionWidth = 0;

};

class PeerListGlobalSearchController : public PeerListSearchController {
public:
	explicit PeerListGlobalSearchController(not_null<Main::Session*> session);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override {
		return false;
	}

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPcontacts_Found &result, mtpRequestId requestId);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	std::map<QString, MTPcontacts_Found> _cache;
	std::map<mtpRequestId, QString> _queries;

};

struct RecipientPremiumRequiredError {
	TextWithEntities text;
};

[[nodiscard]] RecipientPremiumRequiredError WritePremiumRequiredError(
	not_null<UserData*> user);

class RecipientRow : public PeerListRow {
public:
	explicit RecipientRow(
		not_null<PeerData*> peer,
		const style::PeerListItem *maybeLockedSt = nullptr,
		History *maybeHistory = nullptr);

	bool refreshLock(not_null<const style::PeerListItem*> maybeLockedSt);

	[[nodiscard]] static bool ShowLockedError(
		not_null<PeerListController*> controller,
		not_null<PeerListRow*> row,
		Fn<RecipientPremiumRequiredError(not_null<UserData*>)> error);

	[[nodiscard]] History *maybeHistory() const {
		return _maybeHistory;
	}
	[[nodiscard]] bool locked() const {
		return _lockedSt != nullptr;
	}
	void setLocked(const style::PeerListItem *lockedSt) {
		_lockedSt = lockedSt;
	}
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

	void preloadUserpic() override;

private:
	History *_maybeHistory = nullptr;
	const style::PeerListItem *_lockedSt = nullptr;
	bool _resolvePremiumRequired = false;

};

void TrackPremiumRequiredChanges(
	not_null<PeerListController*> controller,
	rpl::lifetime &lifetime);

class ChatsListBoxController : public PeerListController {
public:
	class Row : public RecipientRow {
	public:
		Row(
			not_null<History*> history,
			const style::PeerListItem *maybeLockedSt = nullptr);

		[[nodiscard]] not_null<History*> history() const {
			return maybeHistory();
		}

	};

	ChatsListBoxController(not_null<Main::Session*> session);
	ChatsListBoxController(
		std::unique_ptr<PeerListSearchController> searchController);

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override final;

protected:
	virtual std::unique_ptr<Row> createRow(not_null<History*> history) = 0;
	virtual void prepareViewHook() = 0;
	virtual void updateRowHook(not_null<Row*> row) {
	}
	virtual QString emptyBoxText() const;

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(not_null<History*> history);

};

class PeerListStories final {
public:
	PeerListStories(
		not_null<PeerListController*> controller,
		not_null<Main::Session*> session);

	void prepare(not_null<PeerListDelegate*> delegate);

	void process(not_null<PeerListRow*> row);
	bool handleClick(not_null<PeerData*> peer);

private:
	struct Counts {
		int count = 0;
		int unread = 0;
	};

	void updateColors();
	void updateFor(uint64 id, int count, int unread);
	void applyForRow(
		not_null<PeerListRow*> row,
		int count,
		int unread,
		bool force = false);

	const not_null<PeerListController*> _controller;
	const not_null<Main::Session*> _session;
	PeerListDelegate *_delegate = nullptr;

	QBrush _unreadBrush;
	base::flat_map<uint64, Counts> _counts;
	rpl::lifetime _lifetime;

};

class ContactsBoxController : public PeerListController {
public:
	explicit ContactsBoxController(not_null<Main::Session*> session);
	ContactsBoxController(
		not_null<Main::Session*> session,
		std::unique_ptr<PeerListSearchController> searchController);

	[[nodiscard]] Main::Session &session() const override;
	void prepare() override final;
	[[nodiscard]] std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override final;
	void rowClicked(not_null<PeerListRow*> row) override;
	bool trackSelectedList() override {
		return !_stories;
	}

	enum class SortMode {
		Alphabet,
		Online,
	};
	void setSortMode(SortMode mode);
	void setStoriesShown(bool shown);

protected:
	virtual std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user);
	virtual void prepareViewHook() {
	}
	virtual void updateRowHook(not_null<PeerListRow*> row) {
	}

private:
	void sort();
	void sortByOnline();
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(not_null<UserData*> user);

	const not_null<Main::Session*> _session;
	SortMode _sortMode = SortMode::Alphabet;
	base::Timer _sortByOnlineTimer;
	rpl::lifetime _sortByOnlineLifetime;

	std::unique_ptr<PeerListStories> _stories;

};

struct ChooseRecipientArgs {
	not_null<Main::Session*> session;
	FnMut<void(not_null<Data::Thread*>)> callback;
	Fn<bool(not_null<Data::Thread*>)> filter;

	using PremiumRequiredError = RecipientPremiumRequiredError;
	Fn<PremiumRequiredError(not_null<UserData*>)> premiumRequiredError;
};

class ChooseRecipientBoxController
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	ChooseRecipientBoxController(
		not_null<Main::Session*> session,
		FnMut<void(not_null<Data::Thread*>)> callback,
		Fn<bool(not_null<Data::Thread*>)> filter = nullptr);
	explicit ChooseRecipientBoxController(ChooseRecipientArgs &&args);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

	QString savedMessagesChatStatus() const override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

	bool showLockedError(not_null<PeerListRow*> row);

private:
	const not_null<Main::Session*> _session;
	FnMut<void(not_null<Data::Thread*>)> _callback;
	Fn<bool(not_null<Data::Thread*>)> _filter;
	Fn<RecipientPremiumRequiredError(
		not_null<UserData*>)> _premiumRequiredError;

};

class ChooseTopicSearchController : public PeerListSearchController {
public:
	explicit ChooseTopicSearchController(not_null<Data::Forum*> forum);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	void searchOnServer();
	void searchDone(const MTPcontacts_Found &result, mtpRequestId requestId);

	const not_null<Data::Forum*> _forum;
	MTP::Sender _api;
	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	MsgId _offsetTopicId = 0;
	bool _allLoaded = false;

};

class ChooseTopicBoxController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	ChooseTopicBoxController(
		not_null<Data::Forum*> forum,
		FnMut<void(not_null<Data::ForumTopic*>)> callback,
		Fn<bool(not_null<Data::ForumTopic*>)> filter = nullptr);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void prepare() override;
	void loadMoreRows() override;
	std::unique_ptr<PeerListRow> createSearchRow(PeerListRowId id) override;

private:
	class Row final : public PeerListRow {
	public:
		explicit Row(not_null<Data::ForumTopic*> topic);

		[[nodiscard]] not_null<Data::ForumTopic*> topic() const {
			return _topic;
		}

		QString generateName() override;
		QString generateShortName() override;
		PaintRoundImageCallback generatePaintUserpicCallback(
			bool forceRound) override;

		auto generateNameFirstLetters() const
			-> const base::flat_set<QChar> & override;
		auto generateNameWords() const
			-> const base::flat_set<QString> & override;

	private:
		const not_null<Data::ForumTopic*> _topic;

	};

	void refreshRows(bool initial = false);
	[[nodiscard]] std::unique_ptr<Row> createRow(
		not_null<Data::ForumTopic*> topic);

	const not_null<Data::Forum*> _forum;
	FnMut<void(not_null<Data::ForumTopic*>)> _callback;
	Fn<bool(not_null<Data::ForumTopic*>)> _filter;

};

void PaintPremiumRequiredLock(
	Painter &p,
	not_null<const style::PeerListItem*> st,
	int x,
	int y,
	int outerWidth,
	int size);
