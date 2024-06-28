/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/object_ptr.h"
#include "base/timer.h"
#include "dialogs/dialogs_key.h"
#include "data/data_messages.h"
#include "ui/dragging_scroll_manager.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

namespace style {
struct DialogRow;
} // namespace style

namespace MTP {
class Error;
} // namespace MTP

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class IconButton;
class PopupMenu;
class FlatLabel;
struct ScrollToRequest;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class Thread;
class Folder;
class Forum;
struct ReactionId;
} // namespace Data

namespace Dialogs::Ui {
using namespace ::Ui;
class VideoUserpic;
struct PaintContext;
struct TopicJumpCache;
} // namespace Dialogs::Ui

namespace Dialogs {

class Row;
class FakeRow;
class IndexedList;
class SearchTags;
class SearchEmpty;
class ChatSearchIn;
enum class HashOrCashtag : uchar;

struct ChosenRow {
	Key key;
	Data::MessagePosition message;
	bool userpicClick : 1 = false;
	bool filteredRow : 1 = false;
	bool newWindow : 1 = false;
};

enum class SearchRequestType : uchar {
	FromStart,
	FromOffset,
	PeerFromStart,
	PeerFromOffset,
	MigratedFromStart,
	MigratedFromOffset,
};

enum class SearchRequestDelay : uchar {
	InCache,
	Instant,
	Delayed,
};

enum class WidgetState {
	Default,
	Filtered,
};

class InnerWidget final : public Ui::RpWidget {
public:
	struct ChildListShown {
		PeerId peerId = 0;
		float64 shown = 0.;
	};
	InnerWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<ChildListShown> childListShown);

	void searchReceived(
		std::vector<not_null<HistoryItem*>> result,
		HistoryItem *inject,
		SearchRequestType type,
		int fullCount);
	void peerSearchReceived(
		const QString &query,
		const QVector<MTPPeer> &my,
		const QVector<MTPPeer> &result);

	[[nodiscard]] FilterId filterId() const;

	void clearSelection();

	void changeOpenedFolder(Data::Folder *folder);
	void changeOpenedForum(Data::Forum *forum);
	void showSavedSublists();
	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void dragLeft();
	void setNarrowRatio(float64 narrowRatio);

	void clearFilter();
	void refresh(bool toTop = false);
	void refreshEmpty();
	void resizeEmpty();

	[[nodiscard]] bool isUserpicPress() const;
	[[nodiscard]] bool isUserpicPressOnWide() const;
	void cancelChatPreview();
	bool scheduleChatPreview(QPoint positionOverride);
	bool showChatPreview();
	void chatPreviewShown(bool shown, RowDescriptor row = {});
	bool chooseRow(
		Qt::KeyboardModifiers modifiers = {},
		MsgId pressedTopicRootId = {});

	void scrollToEntry(const RowDescriptor &entry);

	[[nodiscard]] Data::Folder *shownFolder() const;
	[[nodiscard]] Data::Forum *shownForum() const;

	[[nodiscard]] WidgetState state() const;
	[[nodiscard]] not_null<const style::DialogRow*> st() const {
		return _st;
	}
	[[nodiscard]] bool hasFilteredResults() const;

	void searchRequested(bool loading);
	void applySearchState(SearchState state);
	[[nodiscard]] auto searchTagsChanges() const
		-> rpl::producer<std::vector<Data::ReactionId>>;

	void onHashtagFilterUpdate(QStringView newFilter);
	void appendToFiltered(Key key);

	Data::Thread *updateFromParentDrag(QPoint globalPosition);

	void setLoadMoreCallback(Fn<void()> callback);
	void setLoadMoreFilteredCallback(Fn<void()> callback);
	[[nodiscard]] rpl::producer<> listBottomReached() const;
	[[nodiscard]] auto changeSearchTabRequests() const
		-> rpl::producer<ChatSearchTab>;
	[[nodiscard]] rpl::producer<> cancelSearchRequests() const;
	[[nodiscard]] rpl::producer<> cancelSearchFromRequests() const;
	[[nodiscard]] rpl::producer<> changeSearchFromRequests() const;
	[[nodiscard]] rpl::producer<ChosenRow> chosenRow() const;
	[[nodiscard]] rpl::producer<> updated() const;

	[[nodiscard]] rpl::producer<int> scrollByDeltaRequests() const;
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> mustScrollTo() const;
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> dialogMoved() const;
	[[nodiscard]] rpl::producer<SearchRequestDelay> searchRequests() const;
	[[nodiscard]] rpl::producer<QString> completeHashtagRequests() const;
	[[nodiscard]] rpl::producer<> refreshHashtagsRequests() const;

	[[nodiscard]] RowDescriptor resolveChatNext(RowDescriptor from = {}) const;
	[[nodiscard]] RowDescriptor resolveChatPrevious(RowDescriptor from = {}) const;

	~InnerWidget();

	void parentGeometryChanged();

	bool processTouchEvent(not_null<QTouchEvent*> e);
	[[nodiscard]] rpl::producer<> touchCancelRequests() const {
		return _touchCancelRequests.events();
	}

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	struct CollapsedRow;
	struct HashtagResult;
	struct PeerSearchResult;

	enum class JumpSkip {
		PreviousOrBegin,
		NextOrEnd,
		PreviousOrOriginal,
		NextOrOriginal,
	};

	enum class EmptyState : uchar {
		None,
		Loading,
		NoContacts,
		EmptyFolder,
		EmptyForum,
		EmptySavedSublists,
	};

	struct PinnedRow {
		anim::value yadd;
		crl::time animStartTime = 0;
	};

	struct FilterResult {
		FilterResult(not_null<Row*> row) : row(row) {
		}

		not_null<Row*> row;
		int top = 0;

		[[nodiscard]] Key key() const;
		[[nodiscard]] int bottom() const;
	};

	Main::Session &session() const;

	void dialogRowReplaced(Row *oldRow, Row *newRow);

	void setState(WidgetState state);
	void editOpenedFilter();
	void repaintCollapsedFolderRow(not_null<Data::Folder*> folder);
	void refreshWithCollapsedRows(bool toTop = false);
	bool needCollapsedRowsRefresh() const;
	bool chooseCollapsedRow(Qt::KeyboardModifiers modifiers);
	void switchToFilter(FilterId filterId);
	bool chooseHashtag();
	ChosenRow computeChosenRow() const;
	bool isRowActive(not_null<Row*> row, const RowDescriptor &entry) const;
	bool isSearchResultActive(
		not_null<FakeRow*> result,
		const RowDescriptor &entry) const;

	void repaintDialogRow(FilterId filterId, not_null<Row*> row);
	void repaintDialogRow(RowDescriptor row);
	void refreshDialogRow(RowDescriptor row);
	bool updateEntryHeight(not_null<Entry*> entry);

	void clearMouseSelection(bool clearSelection = false);
	void mousePressReleased(
		QPoint globalPosition,
		Qt::MouseButton button,
		Qt::KeyboardModifiers modifiers);
	void processGlobalForceClick(QPoint globalPosition);
	void clearIrrelevantState();
	void selectByMouse(QPoint globalPosition);
	void preloadRowsData();
	void scrollToItem(int top, int height);
	void scrollToDefaultSelected();
	void setCollapsedPressed(int pressed);
	void setPressed(Row *pressed, bool pressedTopicJump);
	void clearPressed();
	void setHashtagPressed(int pressed);
	void setFilteredPressed(int pressed, bool pressedTopicJump);
	void setPeerSearchPressed(int pressed);
	void setSearchedPressed(int pressed);
	bool isPressed() const {
		return (_collapsedPressed >= 0)
			|| _pressed
			|| (_hashtagPressed >= 0)
			|| (_filteredPressed >= 0)
			|| (_peerSearchPressed >= 0)
			|| (_searchedPressed >= 0);
	}
	bool isSelected() const {
		return (_collapsedSelected >= 0)
			|| _selected
			|| (_hashtagSelected >= 0)
			|| (_filteredSelected >= 0)
			|| (_peerSearchSelected >= 0)
			|| (_searchedSelected >= 0);
	}
	bool uniqueSearchResults() const;
	bool hasHistoryInResults(not_null<History*> history) const;

	int defaultRowTop(not_null<Row*> row) const;
	void setupOnlineStatusCheck();
	void jumpToTop();

	void updateRowCornerStatusShown(not_null<History*> history);
	void repaintDialogRowCornerStatus(not_null<History*> history);

	void setupShortcuts();
	RowDescriptor computeJump(
		const RowDescriptor &to,
		JumpSkip skip) const;
	bool jumpToDialogRow(RowDescriptor to);

	RowDescriptor chatListEntryBefore(const RowDescriptor &which) const;
	RowDescriptor chatListEntryAfter(const RowDescriptor &which) const;
	RowDescriptor chatListEntryFirst() const;
	RowDescriptor chatListEntryLast() const;

	void itemRemoved(not_null<const HistoryItem*> item);
	enum class UpdateRowSection {
		Default       = (1 << 0),
		Filtered      = (1 << 1),
		PeerSearch    = (1 << 2),
		MessageSearch = (1 << 3),
		All           = Default | Filtered | PeerSearch | MessageSearch,
	};
	using UpdateRowSections = base::flags<UpdateRowSection>;
	friend inline constexpr auto is_flag_type(UpdateRowSection) { return true; };

	void updateSearchResult(not_null<PeerData*> peer);
	void updateDialogRow(
		RowDescriptor row,
		QRect updateRect = QRect(),
		UpdateRowSections sections = UpdateRowSection::All);
	void fillSupportSearchMenu(not_null<Ui::PopupMenu*> menu);
	void fillArchiveSearchMenu(not_null<Ui::PopupMenu*> menu);

	void refreshShownList();
	[[nodiscard]] int skipTopHeight() const;
	[[nodiscard]] int collapsedRowsOffset() const;
	[[nodiscard]] int dialogsOffset() const;
	[[nodiscard]] int shownHeight(int till = -1) const;
	[[nodiscard]] int fixedOnTopCount() const;
	[[nodiscard]] int pinnedOffset() const;
	[[nodiscard]] int filteredOffset() const;
	[[nodiscard]] int filteredIndex(int y) const;
	[[nodiscard]] int filteredHeight(int till = -1) const;
	[[nodiscard]] int peerSearchOffset() const;
	[[nodiscard]] int searchInChatOffset() const;
	[[nodiscard]] int searchedOffset() const;
	[[nodiscard]] int searchInChatSkip() const;
	[[nodiscard]] int hashtagsOffset() const;

	void paintCollapsedRows(
		Painter &p,
		QRect clip) const;
	void paintCollapsedRow(
		Painter &p,
		not_null<const CollapsedRow*> row,
		bool selected) const;
	void paintPeerSearchResult(
		Painter &p,
		not_null<const PeerSearchResult*> result,
		const Ui::PaintContext &context);
	void paintSearchTags(
		Painter &p,
		const Ui::PaintContext &context) const;
	//void paintSearchInChat(
	//	Painter &p,
	//	const Ui::PaintContext &context) const;
	//void paintSearchInPeer(
	//	Painter &p,
	//	not_null<PeerData*> peer,
	//	Ui::PeerUserpicView &userpic,
	//	int top,
	//	const Ui::Text::String &text) const;
	//void paintSearchInSaved(
	//	Painter &p,
	//	int top,
	//	const Ui::Text::String &text) const;
	//void paintSearchInReplies(
	//	Painter &p,
	//	int top,
	//	const Ui::Text::String &text) const;
	//void paintSearchInTopic(
	//	Painter &p,
	//	const Ui::PaintContext &context,
	//	not_null<Data::ForumTopic*> topic,
	//	Ui::PeerUserpicView &userpic,
	//	int top,
	//	const Ui::Text::String &text) const;
	//template <typename PaintUserpic>
	//void paintSearchInFilter(
	//	Painter &p,
	//	PaintUserpic paintUserpic,
	//	int top,
	//	const style::icon *icon,
	//	const Ui::Text::String &text) const;
	void updateSearchIn();
	void repaintSearchResult(int index);

	Ui::VideoUserpic *validateVideoUserpic(not_null<Row*> row);
	Ui::VideoUserpic *validateVideoUserpic(not_null<History*> history);

	Row *shownRowByKey(Key key);
	void clearSearchResults(bool clearPeerSearchResults = true);
	void updateSelectedRow(Key key = Key());
	void trackSearchResultsHistory(not_null<History*> history);

	[[nodiscard]] QBrush currentBg() const;
	[[nodiscard]] RowDescriptor computeChatPreviewRow() const;

	[[nodiscard]] const std::vector<Key> &pinnedChatsOrder() const;
	void checkReorderPinnedStart(QPoint localPosition);
	void startReorderPinned(QPoint localPosition);
	int updateReorderIndexGetCount();
	bool updateReorderPinned(QPoint localPosition);
	void finishReorderPinned();
	bool finishReorderOnRelease();
	void stopReorderPinned();
	int countPinnedIndex(Row *ofRow);
	void savePinnedOrder();
	bool pinnedShiftAnimationCallback(crl::time now);
	void handleChatListEntryRefreshes();
	void moveSearchIn();
	void dragPinnedFromTouch();

	void saveChatsFilterScrollState(FilterId filterId);
	void restoreChatsFilterScrollState(FilterId filterId);

	const not_null<Window::SessionController*> _controller;

	not_null<IndexedList*> _shownList;
	FilterId _filterId = 0;
	bool _mouseSelection = false;
	std::optional<QPoint> _lastMousePosition;
	int _lastRowLocalMouseX = -1;
	Qt::MouseButton _pressButton = Qt::LeftButton;

	Data::Folder *_openedFolder = nullptr;
	Data::Forum *_openedForum = nullptr;
	rpl::lifetime _openedForumLifetime;

	std::vector<std::unique_ptr<CollapsedRow>> _collapsedRows;
	not_null<const style::DialogRow*> _st;
	mutable std::unique_ptr<Ui::TopicJumpCache> _topicJumpCache;
	int _collapsedSelected = -1;
	int _collapsedPressed = -1;
	bool _skipTopDialog = false;
	Row *_selected = nullptr;
	Row *_pressed = nullptr;
	MsgId _pressedTopicJumpRootId;
	bool _selectedTopicJump = false;
	bool _pressedTopicJump = false;

	Row *_dragging = nullptr;
	int _draggingIndex = -1;
	int _aboveIndex = -1;
	QPoint _dragStart;
	std::vector<PinnedRow> _pinnedRows;
	Ui::Animations::Basic _pinnedShiftAnimation;
	base::flat_set<Key> _pinnedOnDragStart;

	// Remember the last currently dragged row top shift for updating area.
	int _aboveTopShift = -1;
	int _narrowWidth = 0;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	QString _filter, _hashtagFilter;

	std::vector<std::unique_ptr<HashtagResult>> _hashtagResults;
	int _hashtagSelected = -1;
	int _hashtagPressed = -1;
	bool _hashtagDeleteSelected = false;
	bool _hashtagDeletePressed = false;

	std::vector<FilterResult> _filterResults;
	base::flat_map<Key, std::unique_ptr<Row>> _filterResultsGlobal;
	int _filteredSelected = -1;
	int _filteredPressed = -1;

	EmptyState _emptyState = EmptyState::None;

	QString _peerSearchQuery;
	std::vector<std::unique_ptr<PeerSearchResult>> _peerSearchResults;
	int _peerSearchSelected = -1;
	int _peerSearchPressed = -1;

	std::vector<std::unique_ptr<FakeRow>> _searchResults;
	base::flat_set<not_null<History*>> _searchResultsHistories;
	rpl::lifetime _searchResultsLifetime;
	int _searchedCount = 0;
	int _searchedMigratedCount = 0;
	int _searchedSelected = -1;
	int _searchedPressed = -1;

	WidgetState _state = WidgetState::Default;

	std::unique_ptr<ChatSearchIn> _searchIn;
	rpl::event_stream<ChatSearchTab> _changeSearchTabRequests;
	rpl::event_stream<> _cancelSearchRequests;
	rpl::event_stream<> _cancelSearchFromRequests;
	rpl::event_stream<> _changeSearchFromRequests;
	object_ptr<Ui::RpWidget> _loadingAnimation = { nullptr };
	object_ptr<SearchEmpty> _searchEmpty = { nullptr };
	SearchState _searchEmptyState;
	object_ptr<Ui::FlatLabel> _empty = { nullptr };

	Ui::DraggingScrollManager _draggingScroll;

	SearchState _searchState;
	HashOrCashtag _searchHashOrCashtag = {};
	History *_searchInMigrated = nullptr;
	PeerData *_searchFromShown = nullptr;
	Ui::Text::String _searchFromUserText;
	std::unique_ptr<SearchTags> _searchTags;
	int _searchTagsLeft = 0;
	RowDescriptor _menuRow;

	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<Ui::VideoUserpic>> _videoUserpics;

	base::flat_map<FilterId, int> _chatsFilterScrollStates;

	Fn<void()> _loadMoreCallback;
	Fn<void()> _loadMoreFilteredCallback;
	rpl::event_stream<> _listBottomReached;
	rpl::event_stream<ChosenRow> _chosenRow;
	rpl::event_stream<> _updated;

	rpl::event_stream<Ui::ScrollToRequest> _mustScrollTo;
	rpl::event_stream<Ui::ScrollToRequest> _dialogMoved;
	rpl::event_stream<SearchRequestDelay> _searchRequests;
	rpl::event_stream<QString> _completeHashtagRequests;
	rpl::event_stream<> _refreshHashtagsRequests;

	RowDescriptor _chatPreviewRow;
	bool _chatPreviewScheduled = false;
	std::optional<QPoint> _chatPreviewTouchGlobal;
	base::Timer _touchDragPinnedTimer;
	std::optional<QPoint> _touchDragStartGlobal;
	std::optional<QPoint> _touchDragNowGlobal;
	rpl::event_stream<> _touchCancelRequests;

	rpl::variable<ChildListShown> _childListShown;
	float64 _narrowRatio = 0.;
	bool _geometryInited = false;

	bool _savedSublists = false;
	bool _searchLoading = false;
	bool _searchWaiting = false;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Dialogs
