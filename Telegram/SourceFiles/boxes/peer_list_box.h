/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace Ui {
class RippleAnimation;
class RoundImageCheckbox;
class MultiSelect;
template <typename Widget>
class WidgetSlideWrap;
class FlatLabel;
} // namespace Ui

using PeerListRowId = uint64;
class PeerListRow {
public:
	PeerListRow(gsl::not_null<PeerData*> peer);
	PeerListRow(gsl::not_null<PeerData*> peer, PeerListRowId id);

	void setDisabled(bool disabled) {
		_disabled = disabled;
	}

	// Checked state is controlled by the box with multiselect,
	// not by the row itself, so there is no setChecked() method.
	// We can query the checked state from row, but before it is
	// added to the box it is always false.
	bool checked() const;

	gsl::not_null<PeerData*> peer() const {
		return _peer;
	}
	PeerListRowId id() const {
		return _id;
	}

	void setCustomStatus(const QString &status);
	void clearCustomStatus();

	virtual ~PeerListRow();

	// Box interface.
	virtual bool needsVerifiedIcon() const {
		return _peer->isVerified();
	}
	virtual QSize actionSize() const {
		return QSize();
	}
	virtual QMargins actionMargins() const {
		return QMargins();
	}
	virtual void addActionRipple(QPoint point, base::lambda<void()> updateCallback) {
	}
	virtual void stopLastActionRipple() {
	}
	virtual void paintAction(Painter &p, TimeMs ms, int x, int y, int outerWidth, bool actionSelected) {
	}

	void refreshName();
	const Text &name() const {
		return _name;
	}

	enum class StatusType {
		Online,
		LastSeen,
		Custom,
	};
	void refreshStatus();

	void setAbsoluteIndex(int index) {
		_absoluteIndex = index;
	}
	int absoluteIndex() const {
		return _absoluteIndex;
	}
	bool disabled() const {
		return _disabled;
	}
	bool isSearchResult() const {
		return _isSearchResult;
	}
	void setIsSearchResult(bool isSearchResult) {
		_isSearchResult = isSearchResult;
	}

	enum class SetStyle {
		Animated,
		Fast,
	};
	template <typename UpdateCallback>
	void setChecked(bool checked, SetStyle style, UpdateCallback callback) {
		if (checked && !_checkbox) {
			createCheckbox(std::move(callback));
		}
		setCheckedInternal(checked, style);
	}
	void invalidatePixmapsCache();

	template <typename UpdateCallback>
	void addRipple(QSize size, QPoint point, UpdateCallback updateCallback);
	void stopLastRipple();
	void paintRipple(Painter &p, TimeMs ms, int x, int y, int outerWidth);
	void paintUserpic(Painter &p, TimeMs ms, int x, int y, int outerWidth);
	float64 checkedRatio();

	void setNameFirstChars(const OrderedSet<QChar> &nameFirstChars) {
		_nameFirstChars = nameFirstChars;
	}
	const OrderedSet<QChar> &nameFirstChars() const {
		return _nameFirstChars;
	}

	virtual void lazyInitialize();
	virtual void paintStatusText(Painter &p, int x, int y, int availableWidth, int outerWidth, bool selected);

protected:
	bool isInitialized() const {
		return _initialized;
	}

private:
	void createCheckbox(base::lambda<void()> updateCallback);
	void setCheckedInternal(bool checked, SetStyle style);
	void paintDisabledCheckUserpic(Painter &p, int x, int y, int outerWidth) const;
	void setStatusText(const QString &text);

	PeerListRowId _id = 0;
	gsl::not_null<PeerData*> _peer;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	Text _name;
	Text _status;
	StatusType _statusType = StatusType::Online;
	OrderedSet<QChar> _nameFirstChars;
	int _absoluteIndex = -1;
	bool _initialized : 1;
	bool _disabled : 1;
	bool _isSearchResult : 1;

};

enum class PeerListSearchMode {
	Disabled,
	Enabled,
};

class PeerListDelegate {
public:
	virtual void peerListSetTitle(base::lambda<QString()> title) = 0;
	virtual void peerListSetDescription(object_ptr<Ui::FlatLabel> description) = 0;
	virtual void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) = 0;
	virtual void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) = 0;
	virtual void peerListSetSearchMode(PeerListSearchMode mode) = 0;
	virtual void peerListAppendRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendFoundRow(gsl::not_null<PeerListRow*> row) = 0;
	virtual void peerListPrependRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListPrependRowFromSearchResult(gsl::not_null<PeerListRow*> row) = 0;
	virtual void peerListUpdateRow(gsl::not_null<PeerListRow*> row) = 0;
	virtual void peerListRemoveRow(gsl::not_null<PeerListRow*> row) = 0;
	virtual void peerListConvertRowToSearchResult(gsl::not_null<PeerListRow*> row) = 0;
	virtual bool peerListIsRowSelected(gsl::not_null<PeerData*> peer) = 0;
	virtual void peerListSetRowChecked(gsl::not_null<PeerListRow*> row, bool checked) = 0;
	virtual gsl::not_null<PeerListRow*> peerListRowAt(int index) = 0;
	virtual void peerListRefreshRows() = 0;
	virtual void peerListScrollToTop() = 0;
	virtual int peerListFullRowsCount() = 0;
	virtual PeerListRow *peerListFindRow(PeerListRowId id) = 0;
	virtual void peerListSortRows(base::lambda<bool(PeerListRow &a, PeerListRow &b)> compare) = 0;
	virtual void peerListPartitionRows(base::lambda<bool(PeerListRow &a)> border) = 0;

	template <typename PeerDataRange>
	void peerListAddSelectedRows(PeerDataRange &&range) {
		for (auto peer : range) {
			peerListAddSelectedRowInBunch(peer);
		}
		peerListFinishSelectedRowsBunch();
	}

	virtual std::vector<gsl::not_null<PeerData*>> peerListCollectSelectedRows() = 0;
	virtual ~PeerListDelegate() = default;

private:
	virtual void peerListAddSelectedRowInBunch(gsl::not_null<PeerData*> peer) = 0;
	virtual void peerListFinishSelectedRowsBunch() = 0;

};

class PeerListSearchDelegate {
public:
	virtual void peerListSearchAddRow(gsl::not_null<PeerData*> peer) = 0;
	virtual void peerListSearchRefreshRows() = 0;
	virtual ~PeerListSearchDelegate() = default;

};

class PeerListSearchController {
public:
	virtual void searchQuery(const QString &query) = 0;
	virtual bool isLoading() = 0;
	virtual bool loadMoreRows() = 0;
	virtual ~PeerListSearchController() = default;

	void setDelegate(gsl::not_null<PeerListSearchDelegate*> delegate) {
		_delegate = delegate;
	}

protected:
	gsl::not_null<PeerListSearchDelegate*> delegate() const {
		return _delegate;
	}

private:
	PeerListSearchDelegate *_delegate = nullptr;

};

class PeerListController : public PeerListSearchDelegate {
public:
	// Search works only with RowId == peer->id.
	PeerListController(std::unique_ptr<PeerListSearchController> searchController = nullptr);

	void setDelegate(gsl::not_null<PeerListDelegate*> delegate) {
		_delegate = delegate;
		prepare();
	}

	virtual void prepare() = 0;
	virtual void rowClicked(gsl::not_null<PeerListRow*> row) = 0;
	virtual void rowActionClicked(gsl::not_null<PeerListRow*> row) {
	}
	virtual void loadMoreRows() {
	}
	bool isSearchLoading() const {
		return _searchController ? _searchController->isLoading() : false;
	}
	virtual std::unique_ptr<PeerListRow> createSearchRow(gsl::not_null<PeerData*> peer) {
		return std::unique_ptr<PeerListRow>();
	}

	bool isRowSelected(gsl::not_null<PeerData*> peer) {
		return delegate()->peerListIsRowSelected(peer);
	}

	virtual bool searchInLocal() {
		return true;
	}
	bool hasComplexSearch() const;
	void search(const QString &query);

	void peerListSearchAddRow(gsl::not_null<PeerData*> peer) override;
	void peerListSearchRefreshRows() override;

	virtual ~PeerListController() = default;

protected:
	gsl::not_null<PeerListDelegate*> delegate() const {
		return _delegate;
	}
	PeerListSearchController *searchController() const {
		return _searchController.get();
	}

	void setDescriptionText(const QString &text);
	void setSearchLoadingText(const QString &text);
	void setSearchNoResultsText(const QString &text);
	void setDescription(object_ptr<Ui::FlatLabel> description) {
		delegate()->peerListSetDescription(std::move(description));
	}
	void setSearchLoading(object_ptr<Ui::FlatLabel> loading) {
		delegate()->peerListSetSearchLoading(std::move(loading));
	}
	void setSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
		delegate()->peerListSetSearchNoResults(std::move(noResults));
	}

private:
	PeerListDelegate *_delegate = nullptr;
	std::unique_ptr<PeerListSearchController> _searchController = nullptr;

};

class PeerListBox : public BoxContent, public PeerListDelegate {
public:
	PeerListBox(QWidget*, std::unique_ptr<PeerListController> controller, base::lambda<void(PeerListBox*)> init);

	void peerListSetTitle(base::lambda<QString()> title) override {
		setTitle(std::move(title));
	}
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override;
	void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) override;
	void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) override;
	void peerListSetSearchMode(PeerListSearchMode mode) override;
	void peerListAppendRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendFoundRow(gsl::not_null<PeerListRow*> row) override;
	void peerListPrependRow(std::unique_ptr<PeerListRow> row) override;
	void peerListPrependRowFromSearchResult(gsl::not_null<PeerListRow*> row) override;
	void peerListUpdateRow(gsl::not_null<PeerListRow*> row) override;
	void peerListRemoveRow(gsl::not_null<PeerListRow*> row) override;
	void peerListConvertRowToSearchResult(gsl::not_null<PeerListRow*> row) override;
	void peerListSetRowChecked(gsl::not_null<PeerListRow*> row, bool checked) override;
	gsl::not_null<PeerListRow*> peerListRowAt(int index) override;
	bool peerListIsRowSelected(gsl::not_null<PeerData*> peer) override;
	std::vector<gsl::not_null<PeerData*>> peerListCollectSelectedRows() override;
	void peerListRefreshRows() override;
	void peerListScrollToTop() override;
	int peerListFullRowsCount() override;
	PeerListRow *peerListFindRow(PeerListRowId id) override;
	void peerListSortRows(base::lambda<bool(PeerListRow &a, PeerListRow &b)> compare) override;
	void peerListPartitionRows(base::lambda<bool(PeerListRow &a)> border) override;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void peerListAddSelectedRowInBunch(gsl::not_null<PeerData*> peer) override {
		addSelectItem(peer, PeerListRow::SetStyle::Fast);
	}
	void peerListFinishSelectedRowsBunch() override;

	void addSelectItem(gsl::not_null<PeerData*> peer, PeerListRow::SetStyle style);
	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> _select = { nullptr };

	class Inner;
	QPointer<Inner> _inner;

	std::unique_ptr<PeerListController> _controller;
	base::lambda<void(PeerListBox*)> _init;

};

// This class is hold in header because it requires Qt preprocessing.
class PeerListBox::Inner : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, gsl::not_null<PeerListController*> controller);

	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);

	void clearSelection();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void searchQueryChanged(QString query);
	void submitted();

	// Interface for the controller.
	void appendRow(std::unique_ptr<PeerListRow> row);
	void appendSearchRow(std::unique_ptr<PeerListRow> row);
	void appendFoundRow(gsl::not_null<PeerListRow*> row);
	void prependRow(std::unique_ptr<PeerListRow> row);
	void prependRowFromSearchResult(gsl::not_null<PeerListRow*> row);
	PeerListRow *findRow(PeerListRowId id);
	void updateRow(gsl::not_null<PeerListRow*> row) {
		updateRow(row, RowIndex());
	}
	void removeRow(gsl::not_null<PeerListRow*> row);
	void convertRowToSearchResult(gsl::not_null<PeerListRow*> row);
	int fullRowsCount() const;
	gsl::not_null<PeerListRow*> rowAt(int index) const;
	void setDescription(object_ptr<Ui::FlatLabel> description);
	void setSearchLoading(object_ptr<Ui::FlatLabel> loading);
	void setSearchNoResults(object_ptr<Ui::FlatLabel> noResults);
	void refreshRows();

	void setSearchMode(PeerListSearchMode mode);
	void changeCheckState(gsl::not_null<PeerListRow*> row, bool checked, PeerListRow::SetStyle style);

	template <typename ReorderCallback>
	void reorderRows(ReorderCallback &&callback) {
		callback(_rows.begin(), _rows.end());
		for (auto &searchEntity : _searchIndex) {
			callback(searchEntity.second.begin(), searchEntity.second.end());
		}
		refreshIndices();
	}

signals:
	void mustScrollTo(int ymin, int ymax);

public slots:
	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void refreshIndices();
	void removeRowAtIndex(std::vector<std::unique_ptr<PeerListRow>> &from, int index);

	void invalidatePixmapsCache();

	struct RowIndex {
		RowIndex() {
		}
		explicit RowIndex(int value) : value(value) {
		}
		int value = -1;
	};
	friend inline bool operator==(RowIndex a, RowIndex b) {
		return (a.value == b.value);
	}
	friend inline bool operator!=(RowIndex a, RowIndex b) {
		return !(a == b);
	}

	struct Selected {
		Selected() {
		}
		Selected(RowIndex index, bool action) : index(index), action(action) {
		}
		Selected(int index, bool action) : index(index), action(action) {
		}
		RowIndex index;
		bool action = false;
	};
	friend inline bool operator==(Selected a, Selected b) {
		return (a.index == b.index) && (a.action == b.action);
	}
	friend inline bool operator!=(Selected a, Selected b) {
		return !(a == b);
	}

	void setSelected(Selected selected);
	void setPressed(Selected pressed);
	void restoreSelection();

	void updateSelection();
	void loadProfilePhotos();
	void checkScrollForPreload();

	void updateRow(gsl::not_null<PeerListRow*> row, RowIndex hint);
	void updateRow(RowIndex row);
	int getRowTop(RowIndex row) const;
	PeerListRow *getRow(RowIndex element);
	RowIndex findRowIndex(gsl::not_null<PeerListRow*> row, RowIndex hint = RowIndex());
	QRect getActionRect(gsl::not_null<PeerListRow*> row, RowIndex index) const;

	void paintRow(Painter &p, TimeMs ms, RowIndex index);

	void addRowEntry(gsl::not_null<PeerListRow*> row);
	void addToSearchIndex(gsl::not_null<PeerListRow*> row);
	bool addingToSearchIndex() const;
	void removeFromSearchIndex(gsl::not_null<PeerListRow*> row);
	void setSearchQuery(const QString &query, const QString &normalizedQuery);
	bool showingSearch() const {
		return !_searchQuery.isEmpty();
	}
	int shownRowsCount() const {
		return showingSearch() ? _filterResults.size() : _rows.size();
	}
	template <typename Callback>
	bool enumerateShownRows(Callback callback);
	template <typename Callback>
	bool enumerateShownRows(int from, int to, Callback callback);

	int labelHeight() const;

	void clearSearchRows();

	gsl::not_null<PeerListController*> _controller;
	PeerListSearchMode _searchMode = PeerListSearchMode::Disabled;

	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	Selected _selected;
	Selected _pressed;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<PeerListRow>> _rows;
	std::map<PeerListRowId, gsl::not_null<PeerListRow*>> _rowsById;
	std::map<PeerData*, std::vector<gsl::not_null<PeerListRow*>>> _rowsByPeer;

	std::map<QChar, std::vector<gsl::not_null<PeerListRow*>>> _searchIndex;
	QString _searchQuery;
	QString _normalizedSearchQuery;
	QString _mentionHighlight;
	std::vector<gsl::not_null<PeerListRow*>> _filterResults;

	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _searchNoResults = { nullptr };
	object_ptr<Ui::FlatLabel> _searchLoading = { nullptr };

	QPoint _lastMousePosition;

	std::vector<std::unique_ptr<PeerListRow>> _searchRows;

};

class PeerListRowWithLink : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setActionLink(const QString &action);

	void lazyInitialize() override;

private:
	void refreshActionLink();
	QSize actionSize() const override;
	QMargins actionMargins() const override;
	void paintAction(Painter &p, TimeMs ms, int x, int y, int outerWidth, bool actionSelected) override;

	QString _action;
	int _actionWidth = 0;

};

class PeerListGlobalSearchController : public PeerListSearchController, private MTP::Sender {
public:
	PeerListGlobalSearchController();

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override {
		return false;
	}

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPcontacts_Found &result, mtpRequestId requestId);

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	std::map<QString, MTPcontacts_Found> _cache;
	std::map<mtpRequestId, QString> _queries;

};

class ChatsListBoxController : public PeerListController, protected base::Subscriber {
public:
	ChatsListBoxController(std::unique_ptr<PeerListSearchController> searchController = std::make_unique<PeerListGlobalSearchController>());

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(gsl::not_null<PeerData*> peer) override final;

protected:
	class Row : public PeerListRow {
	public:
		Row(gsl::not_null<History*> history) : PeerListRow(history->peer), _history(history) {
		}
		gsl::not_null<History*> history() const {
			return _history;
		}

	private:
		gsl::not_null<History*> _history;

	};
	virtual std::unique_ptr<Row> createRow(gsl::not_null<History*> history) = 0;
	virtual void prepareViewHook() = 0;
	virtual void updateRowHook(Row *row) {
	}

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(History *history);

};
