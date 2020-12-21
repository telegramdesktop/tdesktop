/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/empty_userpic.h"
#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "data/data_cloud_file.h"
#include "base/timer.h"

namespace style {
struct PeerList;
struct PeerListItem;
struct MultiSelect;
} // namespace style

namespace Main {
class Session;
} // namespace Main

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

using PaintRoundImageCallback = Fn<void(
	Painter &p,
	int x,
	int y,
	int outerWidth,
	int size)>;

[[nodiscard]] PaintRoundImageCallback PaintUserpicCallback(
	not_null<PeerData*> peer,
	bool respectSavedMessagesChat);

using PeerListRowId = uint64;
class PeerListRow {
public:
	enum class State {
		Active,
		Disabled,
		DisabledChecked,
	};

	explicit PeerListRow(not_null<PeerData*> peer);
	PeerListRow(not_null<PeerData*> peer, PeerListRowId id);

	virtual ~PeerListRow();

	void setDisabledState(State state) {
		_disabledState = state;
	}

	// Checked state is controlled by the box with multiselect,
	// not by the row itself, so there is no setChecked() method.
	// We can query the checked state from row, but before it is
	// added to the box it is always false.
	[[nodiscard]] bool checked() const;

	[[nodiscard]] bool special() const {
		return !_peer;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		Expects(!special());

		return _peer;
	}
	[[nodiscard]] PeerListRowId id() const {
		return _id;
	}

	[[nodiscard]] std::shared_ptr<Data::CloudImageView> ensureUserpicView();

	[[nodiscard]] virtual QString generateName();
	[[nodiscard]] virtual QString generateShortName();
	[[nodiscard]] virtual auto generatePaintUserpicCallback()
		-> PaintRoundImageCallback;

	void setCustomStatus(const QString &status, bool active = false);
	void clearCustomStatus();

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
	virtual bool actionDisabled() const {
		return false;
	}
	virtual QMargins actionMargins() const {
		return QMargins();
	}
	virtual void addActionRipple(QPoint point, Fn<void()> updateCallback) {
	}
	virtual void stopLastActionRipple() {
	}
	virtual void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	}

	void refreshName(const style::PeerListItem &st);
	const Ui::Text::String &name() const {
		return _name;
	}

	enum class StatusType {
		Online,
		LastSeen,
		Custom,
		CustomActive,
	};
	virtual void refreshStatus();
	crl::time refreshStatusTime() const;

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
	void setIsSavedMessagesChat(bool isSavedMessagesChat) {
		_isSavedMessagesChat = isSavedMessagesChat;
	}
	void setIsRepliesMessagesChat(bool isRepliesMessagesChat) {
		_isRepliesMessagesChat = isRepliesMessagesChat;
	}

	template <typename UpdateCallback>
	void setChecked(
			bool checked,
		const style::RoundImageCheckbox &st,
			anim::type animated,
			UpdateCallback callback) {
		if (checked && !_checkbox) {
			createCheckbox(st, std::move(callback));
		}
		setCheckedInternal(checked, animated);
	}
	void invalidatePixmapsCache();

	template <typename UpdateCallback>
	void addRipple(
		const style::PeerListItem &st,
		QSize size,
		QPoint point,
		UpdateCallback updateCallback);
	void stopLastRipple();
	void paintRipple(Painter &p, int x, int y, int outerWidth);
	void paintUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth);
	float64 checkedRatio();

	void setNameFirstLetters(const base::flat_set<QChar> &firstLetters) {
		_nameFirstLetters = firstLetters;
	}
	const base::flat_set<QChar> &nameFirstLetters() const {
		return _nameFirstLetters;
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

	explicit PeerListRow(PeerListRowId id);

private:
	void createCheckbox(
		const style::RoundImageCheckbox &st,
		Fn<void()> updateCallback);
	void setCheckedInternal(bool checked, anim::type animated);
	void paintDisabledCheckUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) const;
	void setStatusText(const QString &text);

	PeerListRowId _id = 0;
	PeerData *_peer = nullptr;
	mutable std::shared_ptr<Data::CloudImageView> _userpic;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	Ui::Text::String _name;
	Ui::Text::String _status;
	StatusType _statusType = StatusType::Online;
	crl::time _statusValidTill = 0;
	base::flat_set<QChar> _nameFirstLetters;
	int _absoluteIndex = -1;
	State _disabledState = State::Active;
	bool _initialized : 1;
	bool _isSearchResult : 1;
	bool _isSavedMessagesChat : 1;
	bool _isRepliesMessagesChat : 1;

};

enum class PeerListSearchMode {
	Disabled,
	Enabled,
};

struct PeerListState;

class PeerListDelegate {
public:
	virtual void peerListSetTitle(rpl::producer<QString> title) = 0;
	virtual void peerListSetAdditionalTitle(rpl::producer<QString> title) = 0;
	virtual void peerListSetHideEmpty(bool hide) = 0;
	virtual void peerListSetDescription(object_ptr<Ui::FlatLabel> description) = 0;
	virtual void peerListSetSearchLoading(object_ptr<Ui::FlatLabel> loading) = 0;
	virtual void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) = 0;
	virtual void peerListSetAboveWidget(object_ptr<TWidget> aboveWidget) = 0;
	virtual void peerListSetAboveSearchWidget(object_ptr<TWidget> aboveWidget) = 0;
	virtual void peerListSetBelowWidget(object_ptr<TWidget> belowWidget) = 0;
	virtual void peerListSetSearchMode(PeerListSearchMode mode) = 0;
	virtual void peerListAppendRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListAppendFoundRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListPrependRow(std::unique_ptr<PeerListRow> row) = 0;
	virtual void peerListPrependRowFromSearchResult(not_null<PeerListRow*> row) = 0;
	virtual void peerListUpdateRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListRemoveRow(not_null<PeerListRow*> row) = 0;
	virtual void peerListConvertRowToSearchResult(not_null<PeerListRow*> row) = 0;
	virtual bool peerListIsRowChecked(not_null<PeerListRow*> row) = 0;
	virtual void peerListSetRowChecked(not_null<PeerListRow*> row, bool checked) = 0;
	virtual void peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) = 0;
	virtual not_null<PeerListRow*> peerListRowAt(int index) = 0;
	virtual void peerListRefreshRows() = 0;
	virtual void peerListScrollToTop() = 0;
	virtual int peerListFullRowsCount() = 0;
	virtual PeerListRow *peerListFindRow(PeerListRowId id) = 0;
	virtual void peerListSortRows(Fn<bool(const PeerListRow &a, const PeerListRow &b)> compare) = 0;
	virtual int peerListPartitionRows(Fn<bool(const PeerListRow &a)> border) = 0;

	template <typename PeerDataRange>
	void peerListAddSelectedPeers(PeerDataRange &&range) {
		for (const auto peer : range) {
			peerListAddSelectedPeerInBunch(peer);
		}
		peerListFinishSelectedRowsBunch();
	}

	template <typename PeerListRowRange>
	void peerListAddSelectedRows(PeerListRowRange &&range) {
		for (const auto row : range) {
			peerListAddSelectedRowInBunch(row);
		}
		peerListFinishSelectedRowsBunch();
	}

	virtual int peerListSelectedRowsCount() = 0;
	virtual std::unique_ptr<PeerListState> peerListSaveState() const = 0;
	virtual void peerListRestoreState(
		std::unique_ptr<PeerListState> state) = 0;
	virtual ~PeerListDelegate() = default;

private:
	virtual void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) = 0;
	virtual void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) = 0;
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

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

protected:
	not_null<PeerListSearchDelegate*> delegate() const {
		return _delegate;
	}

private:
	PeerListSearchDelegate *_delegate = nullptr;
	rpl::lifetime _lifetime;

};

class PeerListController : public PeerListSearchDelegate {
public:
	struct SavedStateBase {
		virtual ~SavedStateBase() = default;
	};

	// Search works only with RowId == peer->id.
	PeerListController(
		std::unique_ptr<PeerListSearchController> searchController = {});

	void setDelegate(not_null<PeerListDelegate*> delegate) {
		_delegate = delegate;
		prepare();
	}

	void setStyleOverrides(
			const style::PeerList *listSt,
			const style::MultiSelect *selectSt = nullptr) {
		_listSt = listSt;
		_selectSt = selectSt;
	}
	const style::PeerList *listSt() const {
		return _listSt;
	}
	const style::MultiSelect *selectSt() const {
		return _selectSt;
	}
	const style::PeerList &computeListSt() const;
	const style::MultiSelect &computeSelectSt() const;

	virtual void prepare() = 0;
	virtual void rowClicked(not_null<PeerListRow*> row) = 0;
	virtual Main::Session &session() const = 0;
	virtual void rowActionClicked(not_null<PeerListRow*> row) {
	}
	virtual void loadMoreRows() {
	}
	virtual void itemDeselectedHook(not_null<PeerData*> peer) {
	}
	virtual bool isForeignRow(PeerListRowId itemId) {
		return false;
	}
	virtual bool handleDeselectForeignRow(PeerListRowId itemId) {
		return false;
	}
	virtual base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);
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

	virtual std::unique_ptr<PeerListState> saveState() const;
	virtual void restoreState(
		std::unique_ptr<PeerListState> state);

	virtual int contentWidth() const;

	bool isRowSelected(not_null<PeerListRow*> row) {
		return delegate()->peerListIsRowChecked(row);
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

	const style::PeerList *_listSt = nullptr;
	const style::MultiSelect *_selectSt = nullptr;

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
		not_null<PeerListController*> controller);

	struct SkipResult {
		int shouldMoveTo = 0;
		int reallyMovedTo = 0;
	};
	SkipResult selectSkip(int direction);
	void selectSkipPage(int height, int direction);

	[[nodiscard]] rpl::producer<int> selectedIndexValue() const;
	[[nodiscard]] bool hasSelection() const;
	[[nodiscard]] bool hasPressed() const;
	void clearSelection();

	void searchQueryChanged(QString query);
	bool submitted();

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
	void setAboveWidget(object_ptr<TWidget> widget);
	void setAboveSearchWidget(object_ptr<TWidget> widget);
	void setBelowWidget(object_ptr<TWidget> width);
	void setHideEmpty(bool hide);
	void refreshRows();

	void setSearchMode(PeerListSearchMode mode);
	void changeCheckState(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated);

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

	~PeerListContent();

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
	void handleNameChanged(not_null<PeerData*> peer);

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

	void selectByMouse(QPoint globalPosition);
	void loadProfilePhotos();
	void checkScrollForPreload();

	void updateRow(not_null<PeerListRow*> row, RowIndex hint);
	void updateRow(RowIndex row);
	int getRowTop(RowIndex row) const;
	PeerListRow *getRow(RowIndex element);
	RowIndex findRowIndex(not_null<PeerListRow*> row, RowIndex hint = RowIndex());
	QRect getActiveActionRect(not_null<PeerListRow*> row, RowIndex index) const;

	crl::time paintRow(Painter &p, crl::time ms, RowIndex index);

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
	void handleMouseMove(QPoint globalPosition);
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
	rpl::variable<int> _selectedIndex = -1;
	bool _mouseSelection = false;
	std::optional<QPoint> _lastMousePosition;
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
	int _belowHeight = 0;
	bool _hideEmpty = false;
	object_ptr<TWidget> _aboveWidget = { nullptr };
	object_ptr<TWidget> _aboveSearchWidget = { nullptr };
	object_ptr<TWidget> _belowWidget = { nullptr };
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _searchNoResults = { nullptr };
	object_ptr<Ui::FlatLabel> _searchLoading = { nullptr };

	std::vector<std::unique_ptr<PeerListRow>> _searchRows;
	base::Timer _repaintByStatus;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

};

class PeerListContentDelegate : public PeerListDelegate {
public:
	void setContent(PeerListContent *content) {
		_content = content;
	}

	void peerListSetHideEmpty(bool hide) override {
		_content->setHideEmpty(hide);
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
		_content->changeCheckState(row, checked, anim::type::normal);
	}
	void peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) override {
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
	void peerListSetAboveSearchWidget(object_ptr<TWidget> aboveWidget) override {
		_content->setAboveSearchWidget(std::move(aboveWidget));
	}
	void peerListSetBelowWidget(object_ptr<TWidget> belowWidget) override {
		_content->setBelowWidget(std::move(belowWidget));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override {
		_content->setSearchMode(mode);
	}
	void peerListSortRows(
			Fn<bool(const PeerListRow &a, const PeerListRow &b)> compare) override {
		_content->reorderRows([&](
				auto &&begin,
				auto &&end) {
			std::stable_sort(begin, end, [&](auto &&a, auto &&b) {
				return compare(*a, *b);
			});
		});
	}
	int peerListPartitionRows(
			Fn<bool(const PeerListRow &a)> border) override {
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
	: public Ui::BoxContent
	, public PeerListContentDelegate {
public:
	PeerListBox(
		QWidget*,
		std::unique_ptr<PeerListController> controller,
		Fn<void(not_null<PeerListBox*>)> init);

	[[nodiscard]] std::vector<not_null<PeerData*>> collectSelectedRows();

	void peerListSetTitle(rpl::producer<QString> title) override {
		setTitle(std::move(title));
	}
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override {
		setAdditionalTitle(std::move(title));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override;
	void peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) override;
	void peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void peerListAddSelectedPeerInBunch(
			not_null<PeerData*> peer) override {
		addSelectItem(peer, anim::type::instant);
	}
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override {
		addSelectItem(row, anim::type::instant);
	}
	void peerListFinishSelectedRowsBunch() override;

	void addSelectItem(
		not_null<PeerData*> peer,
		anim::type animated);
	void addSelectItem(
		not_null<PeerListRow*> row,
		anim::type animated);
	void addSelectItem(
		uint64 itemId,
		const QString &text,
		PaintRoundImageCallback paintUserpic,
		anim::type animated);
	void createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::SlideWrap<Ui::MultiSelect>> _select = { nullptr };

	std::unique_ptr<PeerListController> _controller;
	Fn<void(PeerListBox*)> _init;
	bool _scrollBottomFixed = false;

};
