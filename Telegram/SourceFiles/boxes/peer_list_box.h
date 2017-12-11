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

#include <rpl/event_stream.h>
#include "ui/rp_widget.h"
#include "ui/empty_userpic.h"
#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace style {
struct PeerList;
struct PeerListItem;
} // namespace style

namespace Ui {
class RippleAnimation;
class RoundImageCheckbox;
class MultiSelect;
template <typename Widget>
class SlideWrap;
class FlatLabel;
struct ScrollToRequest;
class PopupMenu;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

inline auto PaintUserpicCallback(
	not_null<PeerData*> peer,
	bool respectSavedMessagesChat)
->base::lambda<void(Painter &p, int x, int y, int outerWidth, int size)> {
	if (respectSavedMessagesChat && peer->isSelf()) {
		return [](Painter &p, int x, int y, int outerWidth, int size) {
			Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
		};
	}
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
	virtual int nameIconWidth() const;
	virtual void paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected);
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
	virtual void paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	}

	void refreshName(const style::PeerListItem &st);
	const Text &name() const {
		return _name;
	}

	enum class StatusType {
		Online,
		LastSeen,
		Custom,
	};
	void refreshStatus();
	TimeMs refreshStatusTime() const;

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
	bool isSavedMessagesChat() const {
		return _isSavedMessagesChat;
	}
	void setIsSavedMessagesChat(bool isSavedMessagesChat) {
		_isSavedMessagesChat = isSavedMessagesChat;
	}

	enum class SetStyle {
		Animated,
		Fast,
	};
	template <typename UpdateCallback>
	void setChecked(
			bool checked,
			SetStyle style,
			UpdateCallback callback) {
		if (checked && !_checkbox) {
			createCheckbox(std::move(callback));
		}
		setCheckedInternal(checked, style);
	}
	void invalidatePixmapsCache();

	template <typename UpdateCallback>
	void addRipple(
		const style::PeerListItem &st,
		QSize size,
		QPoint point,
		UpdateCallback updateCallback);
	void stopLastRipple();
	void paintRipple(Painter &p, TimeMs ms, int x, int y, int outerWidth);
	void paintUserpic(
		Painter &p,
		const style::PeerListItem &st,
		TimeMs ms,
		int x,
		int y,
		int outerWidth);
	float64 checkedRatio();

	void setNameFirstChars(const base::flat_set<QChar> &nameFirstChars) {
		_nameFirstChars = nameFirstChars;
	}
	const base::flat_set<QChar> &nameFirstChars() const {
		return _nameFirstChars;
	}

	virtual void lazyInitialize(const style::PeerListItem &st);
	virtual void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected);

protected:
	bool isInitialized() const {
		return _initialized;
	}

private:
	void createCheckbox(base::lambda<void()> updateCallback);
	void setCheckedInternal(bool checked, SetStyle style);
	void paintDisabledCheckUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) const;
	void setStatusText(const QString &text);

	PeerListRowId _id = 0;
	not_null<PeerData*> _peer;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	Text _name;
	Text _status;
	StatusType _statusType = StatusType::Online;
	TimeMs _statusValidTill = 0;
	base::flat_set<QChar> _nameFirstChars;
	int _absoluteIndex = -1;
	State _disabledState = State::Active;
	bool _initialized : 1;
	bool _isSearchResult : 1;
	bool _isSavedMessagesChat : 1;

};

enum class PeerListSearchMode {
	Disabled,
	Enabled,
};

struct PeerListState;

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
	virtual void peerListSortRows(base::lambda<bool(const PeerListRow &a, const PeerListRow &b)> compare) = 0;
	virtual int peerListPartitionRows(base::lambda<bool(const PeerListRow &a)> border) = 0;

	template <typename PeerDataRange>
	void peerListAddSelectedRows(PeerDataRange &&range) {
		for (auto peer : range) {
			peerListAddSelectedRowInBunch(peer);
		}
		peerListFinishSelectedRowsBunch();
	}

	virtual int peerListSelectedRowsCount() = 0;
	virtual std::vector<not_null<PeerData*>> peerListCollectSelectedRows() = 0;
	virtual std::unique_ptr<PeerListState> peerListSaveState() const = 0;
	virtual void peerListRestoreState(
		std::unique_ptr<PeerListState> state) = 0;
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
	struct SavedStateBase {
		virtual ~SavedStateBase() = default;
	};

	virtual void searchQuery(const QString &query) = 0;
	virtual bool isLoading() = 0;
	virtual bool loadMoreRows() = 0;
	virtual ~PeerListSearchController() = default;

	void setDelegate(not_null<PeerListSearchDelegate*> delegate) {
		_delegate = delegate;
	}

	virtual std::unique_ptr<SavedStateBase> saveState() const {
		return nullptr;
	}
	virtual void restoreState(
		std::unique_ptr<SavedStateBase> state) {
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
	struct SavedStateBase {
		virtual ~SavedStateBase() = default;
	};

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
	virtual Ui::PopupMenu *rowContextMenu(
			not_null<PeerListRow*> row) {
		return nullptr;
	}
	bool isSearchLoading() const {
		return _searchController ? _searchController->isLoading() : false;
	}
	virtual std::unique_ptr<PeerListRow> createSearchRow(
			not_null<PeerData*> peer) {
		return nullptr;
	}
	virtual std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) {
		return nullptr;
	}

	virtual std::unique_ptr<PeerListState> saveState() const ;
	virtual void restoreState(
		std::unique_ptr<PeerListState> state);

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

	virtual bool respectSavedMessagesChat() const {
		return false;
	}

	virtual rpl::producer<int> onlineCountValue() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

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

	rpl::lifetime _lifetime;

};

struct PeerListState {
	PeerListState() = default;
	PeerListState(PeerListState &&other) = delete;
	PeerListState &operator=(PeerListState &&other) = delete;

	std::unique_ptr<PeerListController::SavedStateBase> controllerState;
	std::vector<not_null<PeerData*>> list;
	std::vector<not_null<PeerData*>> filterResults;
	QString searchQuery;
};

class PeerListContent
	: public Ui::RpWidget
	, private base::Subscriber {
public:
	PeerListContent(
		QWidget *parent,
		not_null<PeerListController*> controller,
		const style::PeerList &st);

	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);

	void clearSelection();

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
		update();
	}

	std::unique_ptr<PeerListState> saveState() const;
	void restoreState(std::unique_ptr<PeerListState> state);

	auto scrollToRequests() const {
		return _scrollToRequests.events();
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

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
	struct SelectedSaved {
		SelectedSaved(PeerListRowId id, Selected old)
		: id(id), old(old) {
		}
		PeerListRowId id = 0;
		Selected old;
	};

	void setSelected(Selected selected);
	void setPressed(Selected pressed);
	void setContexted(Selected contexted);
	void restoreSelection();
	SelectedSaved saveSelectedData(Selected from);
	Selected restoreSelectedData(SelectedSaved from);

	void updateSelection();
	void loadProfilePhotos();
	void checkScrollForPreload();

	void updateRow(not_null<PeerListRow*> row, RowIndex hint);
	void updateRow(RowIndex row);
	int getRowTop(RowIndex row) const;
	PeerListRow *getRow(RowIndex element);
	RowIndex findRowIndex(not_null<PeerListRow*> row, RowIndex hint = RowIndex());
	QRect getActionRect(not_null<PeerListRow*> row, RowIndex index) const;

	TimeMs paintRow(Painter &p, TimeMs ms, RowIndex index);

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
	void clearAllContent();
	void handleMouseMove(QPoint position);
	void mousePressReleased(Qt::MouseButton button);

	const style::PeerList &_st;
	not_null<PeerListController*> _controller;
	PeerListSearchMode _searchMode = PeerListSearchMode::Disabled;

	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	Selected _selected;
	Selected _pressed;
	Selected _contexted;
	bool _mouseSelection = false;
	Qt::MouseButton _pressButton = Qt::LeftButton;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

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
	base::Timer _repaintByStatus;
	QPointer<Ui::PopupMenu> _contextMenu;

};

class PeerListContentDelegate : public PeerListDelegate {
public:
	void setContent(PeerListContent *content) {
		_content = content;
	}

	void peerListAppendRow(
			std::unique_ptr<PeerListRow> row) override {
		_content->appendRow(std::move(row));
	}
	void peerListAppendSearchRow(
			std::unique_ptr<PeerListRow> row) override {
		_content->appendSearchRow(std::move(row));
	}
	void peerListAppendFoundRow(
			not_null<PeerListRow*> row) override {
		_content->appendFoundRow(row);
	}
	void peerListPrependRow(
			std::unique_ptr<PeerListRow> row) override {
		_content->prependRow(std::move(row));
	}
	void peerListPrependRowFromSearchResult(
			not_null<PeerListRow*> row) override {
		_content->prependRowFromSearchResult(row);
	}
	PeerListRow *peerListFindRow(PeerListRowId id) override {
		return _content->findRow(id);
	}
	void peerListUpdateRow(not_null<PeerListRow*> row) override {
		_content->updateRow(row);
	}
	void peerListRemoveRow(not_null<PeerListRow*> row) override {
		_content->removeRow(row);
	}
	void peerListConvertRowToSearchResult(
			not_null<PeerListRow*> row) override {
		_content->convertRowToSearchResult(row);
	}
	void peerListSetRowChecked(
			not_null<PeerListRow*> row,
			bool checked) override {
		_content->changeCheckState(
			row,
			checked,
			PeerListRow::SetStyle::Animated);
	}
	int peerListFullRowsCount() override {
		return _content->fullRowsCount();
	}
	not_null<PeerListRow*> peerListRowAt(int index) override {
		return _content->rowAt(index);
	}
	void peerListRefreshRows() override {
		_content->refreshRows();
	}
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override {
		_content->setDescription(std::move(description));
	}
	void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) override {
		_content->setSearchLoading(std::move(loading));
	}
	void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) override {
		_content->setSearchNoResults(std::move(noResults));
	}
	void peerListSetAboveWidget(object_ptr<TWidget> aboveWidget) override {
		_content->setAboveWidget(std::move(aboveWidget));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override {
		_content->setSearchMode(mode);
	}
	void peerListSortRows(
			base::lambda<bool(const PeerListRow &a, const PeerListRow &b)> compare) override {
		_content->reorderRows([&](
				auto &&begin,
				auto &&end) {
			std::sort(begin, end, [&](auto &&a, auto &&b) {
				return compare(*a, *b);
			});
		});
	}
	int peerListPartitionRows(
			base::lambda<bool(const PeerListRow &a)> border) override {
		auto result = 0;
		_content->reorderRows([&](
				auto &&begin,
				auto &&end) {
			auto edge = std::stable_partition(begin, end, [&](
					auto &&current) {
				return border(*current);
			});
			result = (edge - begin);
		});
		return result;
	}
	std::unique_ptr<PeerListState> peerListSaveState() const override {
		return _content->saveState();
	}
	void peerListRestoreState(
			std::unique_ptr<PeerListState> state) override {
		_content->restoreState(std::move(state));
	}

protected:
	not_null<PeerListContent*> content() const {
		return _content;
	}

private:
	PeerListContent *_content = nullptr;

};

class PeerListBox
	: public BoxContent
	, public PeerListContentDelegate {
public:
	PeerListBox(
		QWidget*,
		std::unique_ptr<PeerListController> controller,
		base::lambda<void(not_null<PeerListBox*>)> init);

	void peerListSetTitle(base::lambda<QString()> title) override {
		setTitle(std::move(title));
	}
	void peerListSetAdditionalTitle(
			base::lambda<QString()> title) override {
		setAdditionalTitle(std::move(title));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override;
	void peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) override;
	bool peerListIsRowSelected(not_null<PeerData*> peer) override;
	int peerListSelectedRowsCount() override;
	std::vector<not_null<PeerData*>> peerListCollectSelectedRows() override;
	void peerListScrollToTop() override;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void peerListAddSelectedRowInBunch(
			not_null<PeerData*> peer) override {
		addSelectItem(peer, PeerListRow::SetStyle::Fast);
	}
	void peerListFinishSelectedRowsBunch() override;

	void addSelectItem(
		not_null<PeerData*> peer,
		PeerListRow::SetStyle style);
	void createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::SlideWrap<Ui::MultiSelect>> _select = { nullptr };

	std::unique_ptr<PeerListController> _controller;
	base::lambda<void(PeerListBox*)> _init;
	bool _scrollBottomFixed = false;

};
