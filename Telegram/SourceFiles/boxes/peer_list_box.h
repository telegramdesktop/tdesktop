/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/empty_userpic.h"
#include "ui/unread_badge.h"
#include "ui/userpic_view.h"
#include "ui/layers/box_content.h"
#include "base/timer.h"

namespace style {
struct PeerList;
struct PeerListItem;
struct MultiSelect;
} // namespace style

namespace Main {
class Session;
class SessionShow;
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
struct OutlineSegment;
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
[[nodiscard]] PaintRoundImageCallback ForceRoundUserpicCallback(
	not_null<PeerData*> peer);

using PeerListRowId = uint64;

[[nodiscard]] PeerListRowId UniqueRowIdFromString(const QString &d);

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

	[[nodiscard]] Ui::PeerUserpicView &ensureUserpicView();

	[[nodiscard]] virtual QString generateName();
	[[nodiscard]] virtual QString generateShortName();
	[[nodiscard]] virtual auto generatePaintUserpicCallback(
		bool forceRound) -> PaintRoundImageCallback;
	virtual void paintUserpicOverlay(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) {
	}

	[[nodiscard]] virtual auto generateNameFirstLetters() const
		-> const base::flat_set<QChar> &;
	[[nodiscard]] virtual auto generateNameWords() const
		-> const base::flat_set<QString> &;
	[[nodiscard]] virtual const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const;

	virtual void preloadUserpic();

	void setCustomStatus(const QString &status, bool active = false);
	void clearCustomStatus();

	// Box interface.
	virtual int paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected);

	virtual QSize rightActionSize() const {
		return QSize();
	}
	virtual QMargins rightActionMargins() const {
		return QMargins();
	}
	virtual bool rightActionDisabled() const {
		return false;
	}
	virtual void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	}
	virtual void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) {
	}
	virtual void rightActionStopLastRipple() {
	}
	[[nodiscard]] virtual float64 opacity() {
		return 1.;
	}

	// By default elements code falls back to a simple right action code.
	virtual int elementsCount() const;
	virtual QRect elementGeometry(int element, int outerWidth) const;
	virtual bool elementDisabled(int element) const;
	virtual bool elementOnlySelect(int element) const;
	virtual void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback);
	virtual void elementsStopLastRipple();
	virtual void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement);

	virtual void refreshName(const style::PeerListItem &st);
	const Ui::Text::String &name() const {
		return _name;
	}

	virtual bool useForumLikeUserpic() const;

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
	void setSavedMessagesChatStatus(QString savedMessagesStatus) {
		_savedMessagesStatus = savedMessagesStatus;
	}
	void setIsRepliesMessagesChat(bool isRepliesMessagesChat) {
		_isRepliesMessagesChat = isRepliesMessagesChat;
	}
	void setIsVerifyCodesChat(bool isVerifyCodesChat) {
		_isVerifyCodesChat = isVerifyCodesChat;
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
	void setCustomizedCheckSegments(
		std::vector<Ui::OutlineSegment> segments);
	void setHidden(bool hidden) {
		_hidden = hidden;
	}
	[[nodiscard]] bool hidden() const {
		return _hidden;
	}
	void finishCheckedAnimation();
	void invalidatePixmapsCache();

	template <typename MaskGenerator, typename UpdateCallback>
	void addRipple(
		const style::PeerListItem &st,
		MaskGenerator &&maskGenerator,
		QPoint point,
		UpdateCallback &&updateCallback);
	void stopLastRipple();
	void paintRipple(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth);
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

	void setSkipPeerBadge(bool skip) {
		_skipPeerBadge = skip;
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
	mutable Ui::PeerUserpicView _userpic;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	std::unique_ptr<Ui::RoundImageCheckbox> _checkbox;
	Ui::Text::String _name;
	Ui::Text::String _status;
	Ui::PeerBadge _badge;
	StatusType _statusType = StatusType::Online;
	crl::time _statusValidTill = 0;
	base::flat_set<QChar> _nameFirstLetters;
	QString _savedMessagesStatus;
	int _absoluteIndex = -1;
	State _disabledState = State::Active;
	bool _hidden : 1 = false;
	bool _initialized : 1 = false;
	bool _isSearchResult : 1 = false;
	bool _isRepliesMessagesChat : 1 = false;
	bool _isVerifyCodesChat : 1 = false;
	bool _skipPeerBadge : 1 = false;

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
	virtual void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) = 0;
	virtual void peerListSetAboveWidget(object_ptr<Ui::RpWidget> aboveWidget) = 0;
	virtual void peerListSetAboveSearchWidget(object_ptr<Ui::RpWidget> aboveWidget) = 0;
	virtual void peerListSetBelowWidget(object_ptr<Ui::RpWidget> belowWidget) = 0;
	virtual void peerListMouseLeftGeometry() = 0;
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
	virtual void peerListSetRowHidden(not_null<PeerListRow*> row, bool hidden) = 0;
	virtual void peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) = 0;
	virtual not_null<PeerListRow*> peerListRowAt(int index) = 0;
	virtual void peerListRefreshRows() = 0;
	virtual void peerListScrollToTop() = 0;
	virtual int peerListFullRowsCount() = 0;
	virtual PeerListRow *peerListFindRow(PeerListRowId id) = 0;
	virtual int peerListSearchRowsCount() = 0;
	virtual not_null<PeerListRow*> peerListSearchRowAt(int index) = 0;
	virtual std::optional<QPoint> peerListLastRowMousePosition() = 0;
	virtual void peerListSortRows(Fn<bool(const PeerListRow &a, const PeerListRow &b)> compare) = 0;
	virtual int peerListPartitionRows(Fn<bool(const PeerListRow &a)> border) = 0;
	virtual std::shared_ptr<Main::SessionShow> peerListUiShow() = 0;

	virtual void peerListSelectSkip(int direction) = 0;

	virtual void peerListPressLeftToContextMenu(bool shown) = 0;
	virtual bool peerListTrackRowPressFromGlobal(QPoint globalPosition) = 0;

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

	virtual void peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed = nullptr) = 0;
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
	virtual void peerListSearchAddRow(PeerListRowId id) = 0;
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
	[[nodiscard]] not_null<PeerListDelegate*> delegate() const {
		return _delegate;
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

	virtual Main::Session &session() const = 0;

	virtual void prepare() = 0;

	virtual void showFinished() {
	}

	virtual void rowClicked(not_null<PeerListRow*> row) = 0;
	virtual void rowMiddleClicked(not_null<PeerListRow*> row) {
	}
	virtual void rowRightActionClicked(not_null<PeerListRow*> row) {
	}

	// By default elements code falls back to a simple right action code.
	virtual void rowElementClicked(not_null<PeerListRow*> row, int element) {
		if (element == 1) {
			rowRightActionClicked(row);
		}
	}

	virtual bool rowTrackPress(not_null<PeerListRow*> row) {
		return false;
	}
	virtual void rowTrackPressCancel() {
	}
	virtual bool rowTrackPressSkipMouseSelection() {
		return false;
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
	virtual std::unique_ptr<PeerListRow> createSearchRow(PeerListRowId id);
	virtual std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) {
		return nullptr;
	}

	virtual std::unique_ptr<PeerListState> saveState() const;
	virtual void restoreState(
		std::unique_ptr<PeerListState> state);

	[[nodiscard]] virtual int contentWidth() const;
	[[nodiscard]] virtual rpl::producer<int> boxHeightValue() const;
	[[nodiscard]] virtual int descriptionTopSkipMin() const;

	[[nodiscard]] bool isRowSelected(not_null<PeerListRow*> row) {
		return delegate()->peerListIsRowChecked(row);
	}

	virtual bool trackSelectedList() {
		return true;
	}
	virtual bool searchInLocal() {
		return true;
	}
	[[nodiscard]] bool hasComplexSearch() const;
	void search(const QString &query);

	void peerListSearchAddRow(not_null<PeerData*> peer) override;
	void peerListSearchAddRow(PeerListRowId id) override;
	void peerListSearchRefreshRows() override;

	[[nodiscard]] virtual QString savedMessagesChatStatus() const {
		return QString();
	}
	[[nodiscard]] virtual int customRowHeight() {
		Unexpected("PeerListController::customRowHeight.");
	}
	virtual void customRowPaint(
			Painter &p,
			crl::time now,
			not_null<PeerListRow*> row,
			bool selected) {
		Unexpected("PeerListController::customRowPaint.");
	}
	[[nodiscard]] virtual bool customRowSelectionPoint(
			not_null<PeerListRow*> row,
			int x,
			int y) {
		Unexpected("PeerListController::customRowSelectionPoint.");
	}
	[[nodiscard]] virtual Fn<QImage()> customRowRippleMaskGenerator() {
		Unexpected("PeerListController::customRowRippleMaskGenerator.");
	}

	virtual bool overrideKeyboardNavigation(
			int direction,
			int fromIndex,
			int toIndex) {
		return false;
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	virtual ~PeerListController() = default;

protected:
	PeerListSearchController *searchController() const {
		return _searchController.get();
	}

	void setDescriptionText(const QString &text);
	void setSearchNoResultsText(const QString &text);
	void setDescription(object_ptr<Ui::FlatLabel> description) {
		delegate()->peerListSetDescription(std::move(description));
	}
	void setSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
		delegate()->peerListSetSearchNoResults(std::move(noResults));
	}

	void sortByName();

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

class PeerListContent : public Ui::RpWidget {
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
	void selectLast();

	enum class Mode {
		Default,
		Custom,
	};
	void setMode(Mode mode);

	[[nodiscard]] rpl::producer<int> selectedIndexValue() const;
	[[nodiscard]] int selectedIndex() const;
	[[nodiscard]] bool hasSelection() const;
	[[nodiscard]] bool hasPressed() const;
	void clearSelection();

	void searchQueryChanged(QString query);
	bool submitted();

	PeerListRowId updateFromParentDrag(QPoint globalPosition);
	void dragLeft();

	void setIgnoreHiddenRowsOnSearch(bool value);

	// Interface for the controller.
	void appendRow(std::unique_ptr<PeerListRow> row);
	void appendSearchRow(std::unique_ptr<PeerListRow> row);
	void appendFoundRow(not_null<PeerListRow*> row);
	void prependRow(std::unique_ptr<PeerListRow> row);
	void prependRowFromSearchResult(not_null<PeerListRow*> row);
	PeerListRow *findRow(PeerListRowId id);
	std::optional<QPoint> lastRowMousePosition() const;
	void updateRow(not_null<PeerListRow*> row) {
		updateRow(row, RowIndex());
	}
	void removeRow(not_null<PeerListRow*> row);
	void convertRowToSearchResult(not_null<PeerListRow*> row);
	int fullRowsCount() const;
	not_null<PeerListRow*> rowAt(int index) const;
	int searchRowsCount() const;
	not_null<PeerListRow*> searchRowAt(int index) const;
	void setDescription(object_ptr<Ui::FlatLabel> description);
	void setSearchLoading(object_ptr<Ui::FlatLabel> loading);
	void setSearchNoResults(object_ptr<Ui::FlatLabel> noResults);
	void setAboveWidget(object_ptr<Ui::RpWidget> widget);
	void setAboveSearchWidget(object_ptr<Ui::RpWidget> widget);
	void setBelowWidget(object_ptr<Ui::RpWidget> width);
	void setHideEmpty(bool hide);
	void refreshRows();

	void mouseLeftGeometry();
	void pressLeftToContextMenu(bool shown);
	bool trackRowPressFromGlobal(QPoint globalPosition);

	void setSearchMode(PeerListSearchMode mode);
	void changeCheckState(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated);
	void setRowHidden(
		not_null<PeerListRow*> row,
		bool hidden);

	template <typename ReorderCallback>
	void reorderRows(ReorderCallback &&callback) {
		callback(_rows.begin(), _rows.end());
		for (auto &searchEntity : _searchIndex) {
			callback(searchEntity.second.begin(), searchEntity.second.end());
		}
		refreshIndices();
		if (!_hiddenRows.empty()) {
			callback(_filterResults.begin(), _filterResults.end());
		}
		update();
	}

	[[nodiscard]] std::unique_ptr<PeerListState> saveState() const;
	void restoreState(std::unique_ptr<PeerListState> state);

	void showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed);

	[[nodiscard]] auto scrollToRequests() const {
		return _scrollToRequests.events();
	}

	[[nodiscard]] auto noSearchSubmits() const {
		return _noSearchSubmits.events();
	}

	~PeerListContent();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
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
		Selected(RowIndex index, int element)
		: index(index)
		, element(element) {
		}
		Selected(int index, int element)
		: index(index)
		, element(element) {
		}

		RowIndex index;
		int element = 0;
	};
	friend inline bool operator==(Selected a, Selected b) {
		return (a.index == b.index) && (a.element == b.element);
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
	RowIndex findRowIndex(
		not_null<PeerListRow*> row,
		RowIndex hint = RowIndex());
	QRect getElementRect(
		not_null<PeerListRow*> row,
		RowIndex index,
		int element) const;

	bool showRowMenu(
		RowIndex index,
		PeerListRow *row,
		QPoint globalPos,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed = nullptr);

	crl::time paintRow(Painter &p, crl::time now, RowIndex index);

	void addRowEntry(not_null<PeerListRow*> row);
	void addToSearchIndex(not_null<PeerListRow*> row);
	bool addingToSearchIndex() const;
	void removeFromSearchIndex(not_null<PeerListRow*> row);
	void setSearchQuery(const QString &query, const QString &normalizedQuery);
	bool showingSearch() const {
		return !_hiddenRows.empty() || !_searchQuery.isEmpty();
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
	void initDecorateWidget(Ui::RpWidget *widget);

	const style::PeerList &_st;
	not_null<PeerListController*> _controller;
	PeerListSearchMode _searchMode = PeerListSearchMode::Disabled;

	Mode _mode = Mode::Default;
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
	std::optional<QPoint> _trackPressStart;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

	std::vector<std::unique_ptr<PeerListRow>> _rows;
	std::map<PeerListRowId, not_null<PeerListRow*>> _rowsById;
	std::map<PeerData*, std::vector<not_null<PeerListRow*>>> _rowsByPeer;

	std::map<QChar, std::vector<not_null<PeerListRow*>>> _searchIndex;
	QString _searchQuery;
	QString _normalizedSearchQuery;
	QString _mentionHighlight;
	std::vector<not_null<PeerListRow*>> _filterResults;
	base::flat_set<not_null<PeerListRow*>> _hiddenRows;

	int _aboveHeight = 0;
	int _belowHeight = 0;
	bool _hideEmpty = false;
	bool _ignoreHiddenRowsOnSearch = false;
	object_ptr<Ui::RpWidget> _aboveWidget = { nullptr };
	object_ptr<Ui::RpWidget> _aboveSearchWidget = { nullptr };
	object_ptr<Ui::RpWidget> _belowWidget = { nullptr };
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _searchNoResults = { nullptr };
	object_ptr<Ui::FlatLabel> _searchLoading = { nullptr };
	object_ptr<Ui::RpWidget> _loadingAnimation = { nullptr };

	rpl::event_stream<> _noSearchSubmits;

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
	std::optional<QPoint> peerListLastRowMousePosition() override {
		return _content->lastRowMousePosition();
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
	void peerListSetRowHidden(
			not_null<PeerListRow*> row,
			bool hidden) override {
		_content->setRowHidden(row, hidden);
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
	int peerListSearchRowsCount() override {
		return _content->searchRowsCount();
	}
	not_null<PeerListRow*> peerListSearchRowAt(int index) override {
		return _content->searchRowAt(index);
	}
	void peerListRefreshRows() override {
		_content->refreshRows();
	}
	void peerListSetDescription(object_ptr<Ui::FlatLabel> description) override {
		_content->setDescription(std::move(description));
	}
	void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel> noResults) override {
		_content->setSearchNoResults(std::move(noResults));
	}
	void peerListSetAboveWidget(object_ptr<Ui::RpWidget> aboveWidget) override {
		_content->setAboveWidget(std::move(aboveWidget));
	}
	void peerListSetAboveSearchWidget(object_ptr<Ui::RpWidget> aboveWidget) override {
		_content->setAboveSearchWidget(std::move(aboveWidget));
	}
	void peerListSetBelowWidget(object_ptr<Ui::RpWidget> belowWidget) override {
		_content->setBelowWidget(std::move(belowWidget));
	}
	void peerListSetSearchMode(PeerListSearchMode mode) override {
		_content->setSearchMode(mode);
	}
	void peerListMouseLeftGeometry() override {
		_content->mouseLeftGeometry();
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
	void peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed = nullptr) override;

	void peerListSelectSkip(int direction) override {
		_content->selectSkip(direction);
	}

	void peerListPressLeftToContextMenu(bool shown) override {
		_content->pressLeftToContextMenu(shown);
	}
	bool peerListTrackRowPressFromGlobal(QPoint globalPosition) override {
		return _content->trackRowPressFromGlobal(globalPosition);
	}

protected:
	not_null<PeerListContent*> content() const {
		return _content;
	}

private:
	PeerListContent *_content = nullptr;

};

class PeerListContentDelegateSimple : public PeerListContentDelegate {
public:
	void peerListSetTitle(rpl::producer<QString> title) override {
	}
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override {
	}
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override {
		return false;
	}
	int peerListSelectedRowsCount() override {
		return 0;
	}
	void peerListScrollToTop() override {
	}
	void peerListAddSelectedPeerInBunch(
			not_null<PeerData*> peer) override {
		Unexpected("...DelegateSimple::peerListAddSelectedPeerInBunch");
	}
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override {
		Unexpected("...DelegateSimple::peerListAddSelectedRowInBunch");
	}
	void peerListFinishSelectedRowsBunch() override {
		Unexpected("...DelegateSimple::peerListFinishSelectedRowsBunch");
	}
	void peerListSetDescription(
			object_ptr<Ui::FlatLabel> description) override {
		description.destroy();
	}
	std::shared_ptr<Main::SessionShow> peerListUiShow() override {
		Unexpected("...DelegateSimple::peerListUiShow");
	}

};

class PeerListContentDelegateShow : public PeerListContentDelegateSimple {
public:
	explicit PeerListContentDelegateShow(
		std::shared_ptr<Main::SessionShow> show);
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;

private:
	std::shared_ptr<Main::SessionShow> _show;

};

class PeerListBox
	: public Ui::BoxContent
	, public PeerListContentDelegate {
public:
	PeerListBox(
		QWidget*,
		std::unique_ptr<PeerListController> controller,
		Fn<void(not_null<PeerListBox*>)> init);

	[[nodiscard]] std::vector<PeerListRowId> collectSelectedIds();
	[[nodiscard]] std::vector<not_null<PeerData*>> collectSelectedRows();
	[[nodiscard]] rpl::producer<int> multiSelectHeightValue() const;
	[[nodiscard]] rpl::producer<> noSearchSubmits() const;

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
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;

	void setAddedTopScrollSkip(int skip, bool aboveSearch = false);

	void showFinished() override;

	void appendQueryChangedCallback(Fn<void(QString)>);

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
	[[nodiscard]] int topScrollSkip() const;
	[[nodiscard]] int topSelectSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::SlideWrap<Ui::MultiSelect>> _select = { nullptr };

	const std::shared_ptr<Main::SessionShow> _show;
	Fn<void(QString)> _customQueryChangedCallback;
	std::unique_ptr<PeerListController> _controller;
	Fn<void(PeerListBox*)> _init;
	bool _scrollBottomFixed = false;
	bool _addedTopScrollAboveSearch = false;
	int _addedTopScrollSkip = 0;

};
