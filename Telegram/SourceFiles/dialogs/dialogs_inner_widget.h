/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_widget.h"
#include "base/flags.h"

namespace Dialogs {
class Row;
class FakeRow;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class IconButton;
class PopupMenu;
class LinkButton;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

class DialogsInner : public Ui::SplittedWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	DialogsInner(QWidget *parent, not_null<Window::Controller*> controller, QWidget *main);

	void dialogsReceived(const QVector<MTPDialog> &dialogs);
	void addSavedPeersAfter(const QDateTime &date);
	void addAllSavedPeers();
	bool searchReceived(const QVector<MTPMessage> &result, DialogsSearchRequestType type, int32 fullCount);
	void peerSearchReceived(const QString &query, const QVector<MTPPeer> &result);
	void showMore(int32 pixels);

	void activate();

	void contactsReceived(const QVector<MTPContact> &result);

	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(PeerData *peer, MsgId msgId);
	void removeDialog(History *history);

	void dragLeft();

	void clearFilter();
	void refresh(bool toTop = false);

	bool choosePeer();
	void saveRecentHashtags(const QString &text);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	Dialogs::IndexedList *contactsNoDialogsList();
	int32 lastSearchDate() const;
	PeerData *lastSearchPeer() const;
	MsgId lastSearchId() const;
	MsgId lastSearchMigratedId() const;

	void setMouseSelection(bool mouseSelection, bool toTop = false);

	enum State {
		DefaultState = 0,
		FilteredState = 1,
		SearchedState = 2,
	};
	void setState(State newState);
	State state() const;
	bool hasFilteredResults() const;

	void searchInPeer(PeerData *peer, UserData *from);

	void onFilterUpdate(QString newFilter, bool force = false);
	void onHashtagFilterUpdate(QStringRef newFilter);

	PeerData *updateFromParentDrag(QPoint globalPos);

	void setLoadMoreCallback(base::lambda<void()> callback) {
		_loadMoreCallback = std::move(callback);
	}

	base::Observable<UserData*> searchFromUserChanged;

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

	~DialogsInner();

public slots:
	void onParentGeometryChanged();
	void onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);

signals:
	void draggingScrollDelta(int delta);
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogMoved(int movedFrom, int movedTo);
	void searchMessages();
	void searchResultChosen();
	void cancelSearchInPeer();
	void completeHashtag(QString tag);
	void refreshHashtags();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintRegion(Painter &p, const QRegion &region, bool paintingOther) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	struct ImportantSwitch;
	using DialogsList = std::unique_ptr<Dialogs::IndexedList>;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	using SearchResults = std::vector<std::unique_ptr<Dialogs::FakeRow>>;
	struct HashtagResult;
	using HashtagResults = std::vector<std::unique_ptr<HashtagResult>>;
	struct PeerSearchResult;
	using PeerSearchResults = std::vector<std::unique_ptr<PeerSearchResult>>;

	void mousePressReleased(Qt::MouseButton button);
	void clearIrrelevantState();
	void updateSelected() {
		updateSelected(mapFromGlobal(QCursor::pos()));
	}
	void updateSelected(QPoint localPos);
	void loadPeerPhotos();
	void setImportantSwitchPressed(bool pressed);
	void setPressed(Dialogs::Row *pressed);
	void setHashtagPressed(int pressed);
	void setFilteredPressed(int pressed);
	void setPeerSearchPressed(int pressed);
	void setSearchedPressed(int pressed);
	bool isPressed() const {
		return _importantSwitchPressed || _pressed || (_hashtagPressed >= 0) || (_filteredPressed >= 0) || (_peerSearchPressed >= 0) || (_searchedPressed >= 0);
	}
	bool isSelected() const {
		return _importantSwitchSelected || _selected || (_hashtagSelected >= 0) || (_filteredSelected >= 0) || (_peerSearchSelected >= 0) || (_searchedSelected >= 0);
	}
	void handlePeerNameChange(not_null<PeerData*> peer, const PeerData::NameFirstChars &oldChars);

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

	void updateDialogRow(PeerData *peer, MsgId msgId, QRect updateRect, UpdateRowSections sections = UpdateRowSection::All);

	int dialogsOffset() const;
	int filteredOffset() const;
	int peerSearchOffset() const;
	int searchedOffset() const;
	int searchInPeerSkip() const;

	void paintDialog(Painter &p, Dialogs::Row *row, int fullWidth, PeerData *active, PeerData *selected, bool onlyBackground, TimeMs ms);
	void paintPeerSearchResult(Painter &p, const PeerSearchResult *result, int fullWidth, bool active, bool selected, bool onlyBackground, TimeMs ms) const;
	void paintSearchInPeer(Painter &p, int fullWidth, bool onlyBackground, TimeMs ms) const;
	void paintSearchInFilter(
		Painter &p,
		PeerData *peer,
		int top,
		int fullWidth,
		const Text &text) const;

	void clearSelection();
	void clearSearchResults(bool clearPeerSearchResults = true);
	void updateSelectedRow(PeerData *peer = 0);

	Dialogs::IndexedList *shownDialogs() const {
		return (Global::DialogsMode() == Dialogs::Mode::Important) ? _dialogsImportant.get() : _dialogs.get();
	}

	void checkReorderPinnedStart(QPoint localPosition);
	int shownPinnedCount() const;
	int updateReorderIndexGetCount();
	bool updateReorderPinned(QPoint localPosition);
	void finishReorderPinned();
	void stopReorderPinned();
	int countPinnedIndex(Dialogs::Row *ofRow);
	void savePinnedOrder();
	void step_pinnedShifting(TimeMs ms, bool timer);

	not_null<Window::Controller*> _controller;

	DialogsList _dialogs;
	DialogsList _dialogsImportant;

	DialogsList _contactsNoDialogs;
	DialogsList _contacts;

	bool _mouseSelection = false;
	QPoint _mouseLastGlobalPosition;
	Qt::MouseButton _pressButton = Qt::LeftButton;

	std::unique_ptr<ImportantSwitch> _importantSwitch;
	bool _importantSwitchSelected = false;
	bool _importantSwitchPressed = false;
	Dialogs::Row *_selected = nullptr;
	Dialogs::Row *_pressed = nullptr;

	Dialogs::Row *_dragging = nullptr;
	int _draggingIndex = -1;
	int _aboveIndex = -1;
	QPoint _dragStart;
	struct PinnedRow {
		anim::value yadd;
		TimeMs animStartTime = 0;
	};
	std::vector<PinnedRow> _pinnedRows;
	BasicAnimation _a_pinnedShifting;
	QList<History*> _pinnedOrder;

	// Remember the last currently dragged row top shift for updating area.
	int _aboveTopShift = -1;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	QString _filter, _hashtagFilter;

	HashtagResults _hashtagResults;
	int _hashtagSelected = -1;
	int _hashtagPressed = -1;
	bool _hashtagDeleteSelected = false;
	bool _hashtagDeletePressed = false;

	FilteredDialogs _filterResults;
	int _filteredSelected = -1;
	int _filteredPressed = -1;

	QString _peerSearchQuery;
	PeerSearchResults _peerSearchResults;
	int _peerSearchSelected = -1;
	int _peerSearchPressed = -1;

	SearchResults _searchResults;
	int _searchedCount = 0;
	int _searchedMigratedCount = 0;
	int _searchedSelected = -1;
	int _searchedPressed = -1;

	int _lastSearchDate = 0;
	PeerData *_lastSearchPeer = nullptr;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;

	State _state = DefaultState;

	object_ptr<Ui::LinkButton> _addContactLnk;
	object_ptr<Ui::IconButton> _cancelSearchInPeer;
	object_ptr<Ui::IconButton> _cancelSearchFromUser;

	PeerData *_searchInPeer = nullptr;
	PeerData *_searchInMigrated = nullptr;
	UserData *_searchFromUser = nullptr;
	Text _searchFromUserText;
	Text _searchInSavedText;
	PeerData *_menuPeer = nullptr;

	base::lambda<void()> _loadMoreCallback;

	base::unique_qptr<Ui::PopupMenu> _menu;

};
