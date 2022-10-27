/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "dialogs/dialogs_key.h"
#include "window/section_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"
#include "ui/special_buttons.h"
#include "mtproto/sender.h"
#include "api/api_single_message_search.h"

namespace MTP {
class Error;
} // namespace MTP

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class TopBarWidget;
class ContactStatus;
} // namespace HistoryView

namespace Ui {
class IconButton;
class PopupMenu;
class DropdownMenu;
class FlatButton;
class InputField;
class CrossButton;
class PlainShadow;
class DownloadBar;
class GroupCallBar;
class RequestsBar;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Window {
class SessionController;
class ConnectionState;
} // namespace Window

namespace Dialogs {

struct RowDescriptor;
class Row;
class FakeRow;
class Key;
struct ChosenRow;
class InnerWidget;
enum class SearchRequestType;

class Widget final : public Window::AbstractSectionWidget {
public:
	Widget(QWidget *parent, not_null<Window::SessionController*> controller);

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	void updateDragInScroll(bool inScroll);

	void searchInChat(Key chat);
	void setInnerFocus();

	void jumpToTop(bool belowPinned = false);

	void startWidthAnimation();
	void stopWidthAnimation();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void showFast();

	void scrollToEntry(const RowDescriptor &entry);

	void searchMessages(const QString &query, Key inChat = {});
	void searchTopics();
	void searchMore();

	void updateForwardBar();

	[[nodiscard]] rpl::producer<> closeForwardBarRequests() const;

	[[nodiscard]] RowDescriptor resolveChatNext(RowDescriptor from = {}) const;
	[[nodiscard]] RowDescriptor resolveChatPrevious(RowDescriptor from = {}) const;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	bool cancelSearch();

	~Widget();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	enum class ShowAnimation {
		External,
		Internal,
	};

	void chosenRow(const ChosenRow &row);
	void listScrollUpdated();
	void cancelSearchInChat();
	void filterCursorMoved();
	void completeHashtag(QString tag);

	[[nodiscard]] QString currentSearchQuery() const;
	void clearSearchField();
	bool searchMessages(bool searchCache = false);
	void needSearchMessages();

	void animationCallback();
	void searchReceived(
		SearchRequestType type,
		const MTPmessages_Messages &result,
		mtpRequestId requestId);
	void peerSearchReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	void escape();
	void submit();
	void cancelSearchRequest();
	[[nodiscard]] PeerData *searchInPeer() const;
	[[nodiscard]] Data::ForumTopic *searchInTopic() const;

	void setupSupportMode();
	void setupConnectingWidget();
	void setupMainMenuToggle();
	void setupDownloadBar();
	void setupShortcuts();
	[[nodiscard]] bool searchForPeersRequired(const QString &query) const;
	[[nodiscard]] bool searchForTopicsRequired(const QString &query) const;
	void setSearchInChat(Key chat, PeerData *from = nullptr);
	void showCalendar();
	void showSearchFrom();
	void showMainMenu();
	void clearSearchCache();
	void updateControlsVisibility(bool fast = false);
	void updateLockUnlockVisibility();
	void updateLoadMoreChatsVisibility();
	void updateJumpToDateVisibility(bool fast = false);
	void updateSearchFromVisibility(bool fast = false);
	void updateControlsGeometry();
	void refreshTopBars();
	void showSearchInTopBar(anim::type animated);
	void checkUpdateStatus();
	void changeOpenedSubsection(
		FnMut<void()> change,
		bool fromRight,
		anim::type animated);
	void changeOpenedFolder(Data::Folder *folder, anim::type animated);
	void changeOpenedForum(ChannelData *forum, anim::type animated);
	QPixmap grabForFolderSlideAnimation();
	void startSlideAnimation();

	void fullSearchRefreshOn(rpl::producer<> events);
	void applyFilterUpdate(bool force = false);
	void refreshLoadMoreButton(bool mayBlock, bool isBlocked);
	void loadMoreBlockedByDate();

	void searchFailed(
		SearchRequestType type,
		const MTP::Error &error,
		mtpRequestId requestId);
	void peopleFailed(const MTP::Error &error, mtpRequestId requestId);

	void scrollToTop();
	void setupScrollUpButton();
	void updateScrollUpVisibility();
	void startScrollUpButtonAnimation(bool shown);
	void updateScrollUpPosition();

	MTP::Sender _api;

	bool _dragInScroll = false;
	bool _dragForward = false;
	base::Timer _chooseByDragTimer;

	object_ptr<Ui::IconButton> _forwardCancel = { nullptr };
	object_ptr<Ui::RpWidget> _searchControls;
	object_ptr<HistoryView::TopBarWidget> _subsectionTopBar = { nullptr } ;
	object_ptr<Ui::IconButton> _mainMenuToggle;
	object_ptr<Ui::IconButton> _searchForNarrowFilters;
	object_ptr<Ui::InputField> _filter;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _chooseFromUser;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _jumpToDate;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<Ui::IconButton> _lockUnlock;

	std::unique_ptr<Ui::PlainShadow> _forumTopShadow;
	std::unique_ptr<Ui::GroupCallBar> _forumGroupCallBar;
	std::unique_ptr<Ui::RequestsBar> _forumRequestsBar;
	std::unique_ptr<HistoryView::ContactStatus> _forumReportBar;

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	class BottomButton;
	object_ptr<BottomButton> _updateTelegram = { nullptr };
	object_ptr<BottomButton> _loadMoreChats = { nullptr };
	std::unique_ptr<Ui::DownloadBar> _downloadBar;
	std::unique_ptr<Window::ConnectionState> _connecting;

	Ui::Animations::Simple _scrollToAnimation;
	Ui::Animations::Simple _a_show;
	Window::SlideDirection _showDirection = Window::SlideDirection();
	QPixmap _cacheUnder, _cacheOver;
	ShowAnimation _showAnimationType = ShowAnimation::External;

	Ui::Animations::Simple _scrollToTopShown;
	object_ptr<Ui::HistoryDownButton> _scrollToTop;
	bool _scrollToTopIsShown = false;
	bool _forumSearchRequested = false;

	Data::Folder *_openedFolder = nullptr;
	ChannelData *_openedForum = nullptr;
	Dialogs::Key _searchInChat;
	History *_searchInMigrated = nullptr;
	PeerData *_searchFromAuthor = nullptr;
	QString _lastFilterText;

	base::Timer _searchTimer;

	QString _peerSearchQuery;
	bool _peerSearchFull = false;
	mtpRequestId _peerSearchRequest = 0;

	QString _topicSearchQuery;
	TimeId _topicSearchOffsetDate = 0;
	MsgId _topicSearchOffsetId = 0;
	MsgId _topicSearchOffsetTopicId = 0;
	bool _topicSearchFull = false;
	mtpRequestId _topicSearchRequest = 0;

	QString _searchQuery;
	PeerData *_searchQueryFrom = nullptr;
	int32 _searchNextRate = 0;
	bool _searchFull = false;
	bool _searchFullMigrated = false;
	int _searchInHistoryRequest = 0; // Not real mtpRequestId.
	mtpRequestId _searchRequest = 0;

	PeerData *_lastSearchPeer = nullptr;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;

	base::flat_map<QString, MTPmessages_Messages> _searchCache;
	Api::SingleMessageSearch _singleMessageSearch;
	base::flat_map<mtpRequestId, QString> _searchQueries;
	base::flat_map<QString, MTPcontacts_Found> _peerSearchCache;
	base::flat_map<mtpRequestId, QString> _peerSearchQueries;

	QPixmap _widthAnimationCache;

	int _topDelta = 0;

	rpl::event_stream<> _closeForwardBarRequests;

};

} // namespace Dialogs
