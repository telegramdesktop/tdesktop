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

namespace Notify {
struct PeerUpdate;
} // namespace Notify

inline auto PaintUserpicCallback(PeerData *peer) {
	return [peer](Painter &p, int x, int y, int outerWidth, int size) {
		peer->paintUserpicLeft(p, x, y, outerWidth, size);
	};
}

using PeerListRowId = uint64;
class PeerListRow {
public:
	PeerListRow(not_null<PeerData*> peer);
	PeerListRow(not_null<PeerData*> peer, PeerListRowId id);

	enum class State {
		Active,
		Disabled,
		DisabledChecked,
	};
	void setDisabledState(State state) {
		_disabledState = state;
	}

	// Checked state is controlled by the box with multiselect,
	// not by the row itself, so there is no setChecked() method.
	// We can query the checked state from row, but before it is
	// added to the box it is always false.
	bool checked() const;

	not_null<PeerData*> peer() const {
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
		return (_disabledState != State::Active);
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
	not_null<PeerData*> _peer;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	Text _name;
	Text _status;
	StatusType _statusType = StatusType::Online;
	OrderedSet<QChar> _nameFirstChars;
	int _absoluteIndex = -1;
	State _disabledState = State::Active;
	bool _initialized : 1;
	bool _isSearchResult : 1;

};

enum class PeerListSearchMode {
	Disabled,
	Enabled,
};

class PeerListDelegate {
public:
	virtual void peerListSetTitle(base::lambda<QString()> title) = 0;
	virtual void peerListSetAdditionalTitle(base::lambda<QString()> title) = 0;
	virtual void peerListSetDescription(object_ptr<Ui::FlatLabel> description) = 0;
	virtual void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) = 0;
	virtual void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) = 0;
	virtual void peerListSetAboveWidget(object_ptr<TWidget> aboveWidget) = 0;
	virtual void peerListSetSearchMode(PeerListSearchMode mode) = 0;
	virtual void peerListAppendRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendFoundRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListPrependRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListPrependRowFromSearchResult(not_null<PeerListRow*> row) = 0;
	virtual void peerListUpdateRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListRemoveRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListConvertRowToSearchResult(not_null<PeerListRow*> row) = 0;
	virtual bool peerListIsRowSelected(not_null<PeerData*> peer) = 0;
	virtual void peerListSetRowChecked(not_null<PeerListRow*> row, bool checked) = 0;
	virtual not_null<PeerListRow*> peerListRowAt(int index) = 0;
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

	virtual int peerListSelectedRowsCount() = 0;
	virtual std::vector<not_null<PeerData*>> peerListCollectSelectedRows() = 0;
	virtual ~PeerListDelegate() = default;

private:
	virtual void peerListAddSelectedRowInBunch(not_null<PeerData*> peer) = 0;
	virtual void peerListFinishSelectedRowsBunch() = 0;

};

class PeerListSearchDelegate {
public:
	virtual void peerListSearchAddRow(not_null<PeerData*> peer) = 0;
	virtual void peerListSearchRefreshRows() = 0;
	virtual ~PeerListSearchDelegate() = default;

};

class PeerListSearchController {
public:
	virtual void searchQuery(const QString &query) = 0;
	virtual bool isLoading() = 0;
	virtual bool loadMoreRows() = 0;
	virtual ~PeerListSearchController() = default;

	void setDelegate(not_null<PeerListSearchDelegate*> delegate) {
		_delegate = delegate;
	}

protected:
	not_null<PeerListSearchDelegate*> delegate() const {
		return _delegate;
	}

private:
	PeerListSearchDelegate *_delegate = nullptr;

};

class PeerListController : public PeerListSearchDelegate {
public:
	// Search works only with RowId == peer->id.
	PeerListController(std::unique_ptr<PeerListSearchController> searchController = nullptr);

	void setDelegate(not_null<PeerListDelegate*> delegate) {
		_delegate = delegate;
		prepare();
	}

	virtual void prepare() = 0;
	virtual void rowClicked(not_null<PeerListRow*> row) = 0;
	virtual void rowActionClicked(not_null<PeerListRow*> row) {
	}
	virtual void loadMoreRows() {
	}
	virtual void itemDeselectedHook(not_null<PeerData*> peer) {
	}
	bool isSearchLoading() const {
		return _searchController ? _searchController->isLoading() : false;
	}
	virtual std::unique_ptr<PeerListRow> createSearchRow(not_null<PeerData*> peer) {
		return nullptr;
	}

	bool isRowSelected(not_null<PeerData*> peer) {
		return delegate()->peerListIsRowSelected(peer);
	}

	virtual bool searchInLocal() {
		return true;
	}
	bool hasComplexSearch() const;
	void search(const QString &query);

	void peerListSearchAddRow(not_null<PeerData*> peer) override;
	void peerListSearchRefreshRows() override;

	virtual ~PeerListController() = default;

protected:
	not_null<PeerListDelegate*> delegate() const {
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
	PeerListBox(QWidget*, std::unique_ptr<PeerListController> controller, base::lambda<void(not_null<PeerListBox*>)> init);

	void peerListSetTitle(base::lambda<QString()> title) override {
		setTitle(std::move(title));
	}
	void peerListSetAdditionalTitle(base::lambda<QString()> title) override {
		setAdditionalTitle(std::move(title));
	}
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override;
	void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) override;
	void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) override;
	void peerListSetAboveWidget(object_ptr<TWidget> aboveWidget) override;
	void peerListSetSearchMode(PeerListSearchMode mode) override;
	void peerListAppendRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendFoundRow(not_null<PeerListRow*> row) override;
	void peerListPrependRow(std::unique_ptr<PeerListRow> row) override;
	void peerListPrependRowFromSearchResult(not_null<PeerListRow*> row) override;
	void peerListUpdateRow(not_null<PeerListRow*> row) override;
	void peerListRemoveRow(not_null<PeerListRow*> row) override;
	void peerListConvertRowToSearchResult(not_null<PeerListRow*> row) override;
	void peerListSetRowChecked(not_null<PeerListRow*> row, bool checked) override;
	not_null<PeerListRow*> peerListRowAt(int index) override;
	bool peerListIsRowSelected(not_null<PeerData*> peer) override;
	int peerListSelectedRowsCount() override;
	std::vector<not_null<PeerData*>> peerListCollectSelectedRows() override;
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
	void peerListAddSelectedRowInBunch(not_null<PeerData*> peer) override {
		addSelectItem(peer, PeerListRow::SetStyle::Fast);
	}
	void peerListFinishSelectedRowsBunch() override;

	void addSelectItem(not_null<PeerData*> peer, PeerListRow::SetStyle style);
	void createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> _select = { nullptr };

	class Inner;
	QPointer<Inner> _inner;

	std::unique_ptr<PeerListController> _controller;
	base::lambda<void(PeerListBox*)> _init;
	bool _scrollBottomFixed = true;

};

// This class is hold in header because it requires Qt preprocessing.
class PeerListBox::Inner : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, not_null<PeerListController*> controller);

	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);

	void clearSelection();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void searchQueryChanged(QString query);
	void submitted();

	// Interface for the controller.
	void appendRow(std::unique_ptr<PeerListRow> row);
	void appendSearchRow(std::unique_ptr<PeerListRow> row);
	void appendFoundRow(not_null<PeerListRow*> row);
	void prependRow(std::unique_ptr<PeerListRow> row);
	void prependRowFromSearchResult(not_null<PeerListRow*> row);
	PeerListRow *findRow(PeerListRowId id);
	void updateRow(not_null<PeerListRow*> row) {
		updateRow(row, RowIndex());
	}
	void removeRow(not_null<PeerListRow*> row);
	void convertRowToSearchResult(not_null<PeerListRow*> row);
	int fullRowsCount() const;
	not_null<PeerListRow*> rowAt(int index) const;
	void setDescription(object_ptr<Ui::FlatLabel> description);
	void setSearchLoading(object_ptr<Ui::FlatLabel> loading);
	void setSearchNoResults(object_ptr<Ui::FlatLabel> noResults);
	void setAboveWidget(object_ptr<TWidget> aboveWidget);
	void refreshRows();

	void setSearchMode(PeerListSearchMode mode);
	void changeCheckState(not_null<PeerListRow*> row, bool checked, PeerListRow::SetStyle style);

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

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void refreshIndices();
	void removeRowAtIndex(std::vector<std::unique_ptr<PeerListRow>> &from, int index);
	void handleNameChanged(const Notify::PeerUpdate &update);

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

	void updateRow(not_null<PeerListRow*> row, RowIndex hint);
	void updateRow(RowIndex row);
	int getRowTop(RowIndex row) const;
	PeerListRow *getRow(RowIndex element);
	RowIndex findRowIndex(not_null<PeerListRow*> row, RowIndex hint = RowIndex());
	QRect getActionRect(not_null<PeerListRow*> row, RowIndex index) const;

	void paintRow(Painter &p, TimeMs ms, RowIndex index);

	void addRowEntry(not_null<PeerListRow*> row);
	void addToSearchIndex(not_null<PeerListRow*> row);
	bool addingToSearchIndex() const;
	void removeFromSearchIndex(not_null<PeerListRow*> row);
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

	int rowsTop() const;
	int labelHeight() const;

	void clearSearchRows();

	not_null<PeerListController*> _controller;
	PeerListSearchMode _searchMode = PeerListSearchMode::Disabled;

	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	Selected _selected;
	Selected _pressed;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<PeerListRow>> _rows;
	std::map<PeerListRowId, not_null<PeerListRow*>> _rowsById;
	std::map<PeerData*, std::vector<not_null<PeerListRow*>>> _rowsByPeer;

	std::map<QChar, std::vector<not_null<PeerListRow*>>> _searchIndex;
	QString _searchQuery;
	QString _normalizedSearchQuery;
	QString _mentionHighlight;
	std::vector<not_null<PeerListRow*>> _filterResults;

	int _aboveHeight = 0;
	object_ptr<TWidget> _aboveWidget = { nullptr };
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _searchNoResults = { nullptr };
	object_ptr<Ui::FlatLabel> _searchLoading = { nullptr };

	QPoint _lastMousePosition;

	std::vector<std::unique_ptr<PeerListRow>> _searchRows;

};
