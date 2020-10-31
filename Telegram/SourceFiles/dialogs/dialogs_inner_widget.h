/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"
#include "data/data_messages.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/flags.h"
#include "base/object_ptr.h"

class RPCError;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class IconButton;
class PopupMenu;
class FlatLabel;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class CloudImageView;
} // namespace Data

namespace Dialogs {

class Row;
class FakeRow;
class IndexedList;

struct ChosenRow {
	Key key;
	Data::MessagePosition message;
	bool filteredRow = false;
};

enum class SearchRequestType {
	FromStart,
	FromOffset,
	PeerFromStart,
	PeerFromOffset,
	MigratedFromStart,
	MigratedFromOffset,
};

enum class WidgetState {
	Default,
	Filtered,
};

class InnerWidget final : public Ui::RpWidget, private base::Subscriber {
	Q_OBJECT

public:
	InnerWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool searchReceived(
		const QVector<MTPMessage> &result,
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
	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void dragLeft();

	void clearFilter();
	void refresh(bool toTop = false);
	void refreshEmptyLabel();
	void resizeEmptyLabel();

	bool chooseRow();

	void scrollToEntry(const RowDescriptor &entry);

	Data::Folder *shownFolder() const;
	int32 lastSearchDate() const;
	PeerData *lastSearchPeer() const;
	MsgId lastSearchId() const;
	MsgId lastSearchMigratedId() const;

	WidgetState state() const;
	bool waitingForSearch() const {
		return _waitingForSearch;
	}
	bool hasFilteredResults() const;

	void searchInChat(Key key, PeerData *from);

	void applyFilterUpdate(QString newFilter, bool force = false);
	void onHashtagFilterUpdate(QStringRef newFilter);

	PeerData *updateFromParentDrag(QPoint globalPosition);

	void setLoadMoreCallback(Fn<void()> callback);
	[[nodiscard]] rpl::producer<> listBottomReached() const;

	base::Observable<PeerData*> searchFromUserChanged;

	[[nodiscard]] rpl::producer<ChosenRow> chosenRow() const;
	[[nodiscard]] rpl::producer<> updated() const;

	~InnerWidget();

public slots:
	void onParentGeometryChanged();

signals:
	void draggingScrollDelta(int delta);
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogMoved(int movedFrom, int movedTo);
	void searchMessages();
	void cancelSearchInChat();
	void completeHashtag(QString tag);
	void refreshHashtags();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void enterEventHook(QEvent *e) override;
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
	};

	Main::Session &session() const;

	void dialogRowReplaced(Row *oldRow, Row *newRow);

	void editOpenedFilter();
	void repaintCollapsedFolderRow(not_null<Data::Folder*> folder);
	void refreshWithCollapsedRows(bool toTop = false);
	bool needCollapsedRowsRefresh() const;
	bool chooseCollapsedRow();
	void switchToFilter(FilterId filterId);
	bool chooseHashtag();
	ChosenRow computeChosenRow() const;
	bool isSearchResultActive(
		not_null<FakeRow*> result,
		const RowDescriptor &entry) const;

	void repaintDialogRow(FilterId filterId, not_null<Row*> row);
	void repaintDialogRow(RowDescriptor row);
	void refreshDialogRow(RowDescriptor row);

	void clearMouseSelection(bool clearSelection = false);
	void mousePressReleased(QPoint globalPosition, Qt::MouseButton button);
	void clearIrrelevantState();
	void selectByMouse(QPoint globalPosition);
	void loadPeerPhotos();
	void setCollapsedPressed(int pressed);
	void setPressed(Row *pressed);
	void setHashtagPressed(int pressed);
	void setFilteredPressed(int pressed);
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
	void userOnlineUpdated(not_null<PeerData*> peer);

	void setupShortcuts();
	RowDescriptor computeJump(
		const RowDescriptor &to,
		JumpSkip skip);
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

	int dialogsOffset() const;
	int fixedOnTopCount() const;
	int pinnedOffset() const;
	int filteredOffset() const;
	int peerSearchOffset() const;
	int searchedOffset() const;
	int searchInChatSkip() const;

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
		int fullWidth,
		bool active,
		bool selected) const;
	void paintSearchInChat(Painter &p) const;
	void paintSearchInPeer(
		Painter &p,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &userpic,
		int top,
		const Ui::Text::String &text) const;
	void paintSearchInSaved(
		Painter &p,
		int top,
		const Ui::Text::String &text) const;
	void paintSearchInReplies(
		Painter &p,
		int top,
		const Ui::Text::String &text) const;
	//void paintSearchInFeed( // #feed
	//	Painter &p,
	//	not_null<Data::Feed*> feed,
	//	int top,
	//	const Ui::Text::String &text) const;
	template <typename PaintUserpic>
	void paintSearchInFilter(
		Painter &p,
		PaintUserpic paintUserpic,
		int top,
		const style::icon *icon,
		const Ui::Text::String &text) const;
	void refreshSearchInChatLabel();

	void clearSearchResults(bool clearPeerSearchResults = true);
	void updateSelectedRow(Key key = Key());

	not_null<IndexedList*> shownDialogs() const;

	void checkReorderPinnedStart(QPoint localPosition);
	int updateReorderIndexGetCount();
	bool updateReorderPinned(QPoint localPosition);
	void finishReorderPinned();
	void stopReorderPinned();
	int countPinnedIndex(Row *ofRow);
	void savePinnedOrder();
	bool pinnedShiftAnimationCallback(crl::time now);
	void handleChatListEntryRefreshes();

	not_null<Window::SessionController*> _controller;

	FilterId _filterId = 0;
	bool _mouseSelection = false;
	std::optional<QPoint> _lastMousePosition;
	Qt::MouseButton _pressButton = Qt::LeftButton;

	Data::Folder *_openedFolder = nullptr;

	std::vector<std::unique_ptr<CollapsedRow>> _collapsedRows;
	int _collapsedSelected = -1;
	int _collapsedPressed = -1;
	int _skipTopDialogs = 0;
	Row *_selected = nullptr;
	Row *_pressed = nullptr;

	Row *_dragging = nullptr;
	int _draggingIndex = -1;
	int _aboveIndex = -1;
	QPoint _dragStart;
	struct PinnedRow {
		anim::value yadd;
		crl::time animStartTime = 0;
	};
	std::vector<PinnedRow> _pinnedRows;
	Ui::Animations::Basic _pinnedShiftAnimation;
	base::flat_set<Key> _pinnedOnDragStart;

	// Remember the last currently dragged row top shift for updating area.
	int _aboveTopShift = -1;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	QString _filter, _hashtagFilter;

	std::vector<std::unique_ptr<HashtagResult>> _hashtagResults;
	int _hashtagSelected = -1;
	int _hashtagPressed = -1;
	bool _hashtagDeleteSelected = false;
	bool _hashtagDeletePressed = false;

	std::vector<not_null<Row*>> _filterResults;
	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<Row>> _filterResultsGlobal;
	int _filteredSelected = -1;
	int _filteredPressed = -1;

	bool _waitingForSearch = false;
	EmptyState _emptyState = EmptyState::None;

	QString _peerSearchQuery;
	std::vector<std::unique_ptr<PeerSearchResult>> _peerSearchResults;
	int _peerSearchSelected = -1;
	int _peerSearchPressed = -1;

	std::vector<std::unique_ptr<FakeRow>> _searchResults;
	int _searchedCount = 0;
	int _searchedMigratedCount = 0;
	int _searchedSelected = -1;
	int _searchedPressed = -1;

	int _lastSearchDate = 0;
	PeerData *_lastSearchPeer = nullptr;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;

	WidgetState _state = WidgetState::Default;

	object_ptr<Ui::FlatLabel> _empty = { nullptr };
	object_ptr<Ui::IconButton> _cancelSearchInChat;
	object_ptr<Ui::IconButton> _cancelSearchFromUser;

	Key _searchInChat;
	History *_searchInMigrated = nullptr;
	PeerData *_searchFromPeer = nullptr;
	mutable std::shared_ptr<Data::CloudImageView> _searchInChatUserpic;
	mutable std::shared_ptr<Data::CloudImageView> _searchFromUserUserpic;
	Ui::Text::String _searchInChatText;
	Ui::Text::String _searchFromUserText;
	RowDescriptor _menuRow;

	Fn<void()> _loadMoreCallback;
	rpl::event_stream<> _listBottomReached;
	rpl::event_stream<ChosenRow> _chosenRow;
	rpl::event_stream<> _updated;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Dialogs
