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
#include "ui/userpic_view.h"
#include "mtproto/sender.h"
#include "api/api_single_message_search.h"

namespace MTP {
class Error;
} // namespace MTP

namespace Data {
class Forum;
enum class StorySourcesList : uchar;
struct ReactionId;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class TopBarWidget;
class ContactStatus;
} // namespace HistoryView

namespace Ui {
class AbstractButton;
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
class MoreChatsBar;
class JumpDownButton;
class ElasticScroll;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Window {
class SessionController;
class ConnectionState;
struct SectionShow;
} // namespace Window

namespace Dialogs::Stories {
class List;
struct Content;
} // namespace Dialogs::Stories

namespace Dialogs {

extern const char kOptionForumHideChatsList[];

struct RowDescriptor;
class Row;
class FakeRow;
class Key;
struct ChosenRow;
class InnerWidget;
enum class SearchRequestType;
class Suggestions;
class ChatSearchIn;
enum class ChatSearchTab : uchar;

class Widget final : public Window::AbstractSectionWidget {
public:
	enum class Layout {
		Main,
		Child,
	};
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Layout layout);

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	void updateDragInScroll(bool inScroll);

	void showForum(
		not_null<Data::Forum*> forum,
		const Window::SectionShow &params);
	void setInnerFocus(bool unfocusSearch = false);
	[[nodiscard]] bool searchHasFocus() const;

	void jumpToTop(bool belowPinned = false);
	void raiseWithTooltip();

	[[nodiscard]] QPixmap grabNonNarrowScrollFrame();
	void startWidthAnimation();
	void stopWidthAnimation();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params);
	void showFast();
	[[nodiscard]] rpl::producer<float64> shownProgressValue() const;

	void scrollToEntry(const RowDescriptor &entry);

	void searchMessages(SearchState state);

	[[nodiscard]] RowDescriptor resolveChatNext(RowDescriptor from = {}) const;
	[[nodiscard]] RowDescriptor resolveChatPrevious(RowDescriptor from = {}) const;
	void updateHasFocus(not_null<QWidget*> focused);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	bool cancelSearch(bool forceFullCancel = false);
	bool cancelSearchByMouseBack();

	QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

	~Widget();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void inputMethodEvent(QInputMethodEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void chosenRow(const ChosenRow &row);
	void listScrollUpdated();
	void searchCursorMoved();
	void completeHashtag(QString tag);

	[[nodiscard]] QString currentSearchQuery() const;
	[[nodiscard]] int currentSearchQueryCursorPosition() const;
	void clearSearchField();
	void searchRequested();
	bool search(bool inCache = false);
	void searchTopics();
	void searchMore();

	void slideFinished();
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
	[[nodiscard]] PeerData *searchFromPeer() const;
	[[nodiscard]] const std::vector<Data::ReactionId> &searchInTags() const;

	void setupSupportMode();
	void setupTouchChatPreview();
	void setupConnectingWidget();
	void setupMainMenuToggle();
	void setupMoreChatsBar();
	void setupDownloadBar();
	void setupShortcuts();
	void setupStories();
	void storiesExplicitCollapse();
	void collectStoriesUserpicsViews(Data::StorySourcesList list);
	void storiesToggleExplicitExpand(bool expand);
	void trackScroll(not_null<Ui::RpWidget*> widget);
	[[nodiscard]] bool searchForPeersRequired(const QString &query) const;
	[[nodiscard]] bool searchForTopicsRequired(const QString &query) const;

	// Child list may be unable to set specific search state.
	bool applySearchState(SearchState state);

	void showCalendar();
	void showSearchFrom();
	void showMainMenu();
	void clearSearchCache();
	void setSearchQuery(const QString &query, int cursorPosition = -1);
	void updateControlsVisibility(bool fast = false);
	void updateLockUnlockVisibility(
		anim::type animated = anim::type::instant);
	void updateLoadMoreChatsVisibility();
	void updateStoriesVisibility();
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
	void changeOpenedForum(Data::Forum *forum, anim::type animated);
	void hideChildList();
	void destroyChildListCanvas();
	[[nodiscard]] QPixmap grabForFolderSlideAnimation();
	void startSlideAnimation(
		QPixmap oldContentCache,
		QPixmap newContentCache,
		Window::SlideDirection direction);

	void openChildList(
		not_null<Data::Forum*> forum,
		const Window::SectionShow &params);
	void closeChildList(anim::type animated);

	void fullSearchRefreshOn(rpl::producer<> events);
	void updateCancelSearch();
	[[nodiscard]] QString validateSearchQuery();
	void applySearchUpdate();
	void refreshLoadMoreButton(bool mayBlock, bool isBlocked);
	void loadMoreBlockedByDate();

	void searchFailed(
		SearchRequestType type,
		const MTP::Error &error,
		mtpRequestId requestId);
	void peerSearchFailed(const MTP::Error &error, mtpRequestId requestId);
	void searchApplyEmpty(SearchRequestType type, mtpRequestId id);
	void peerSearchApplyEmpty(mtpRequestId id);

	void updateForceDisplayWide();
	void scrollToDefault(bool verytop = false);
	void scrollToDefaultChecked(bool verytop = false);
	void setupScrollUpButton();
	void updateScrollUpVisibility();
	void startScrollUpButtonAnimation(bool shown);
	void updateScrollUpPosition();
	void updateLockUnlockPosition();
	void updateSuggestions(anim::type animated);
	void processSearchFocusChange();

	[[nodiscard]] bool redirectToSearchPossible() const;
	[[nodiscard]] bool redirectKeyToSearch(QKeyEvent *e) const;
	[[nodiscard]] bool redirectImeToSearch() const;

	MTP::Sender _api;

	bool _dragInScroll = false;
	bool _dragForward = false;
	base::Timer _chooseByDragTimer;

	Layout _layout = Layout::Main;
	int _narrowWidth = 0;
	object_ptr<Ui::RpWidget> _searchControls;
	object_ptr<HistoryView::TopBarWidget> _subsectionTopBar = { nullptr };
	struct {
		object_ptr<Ui::IconButton> toggle;
		object_ptr<Ui::AbstractButton> under;
	} _mainMenu;
	object_ptr<Ui::IconButton> _searchForNarrowLayout;
	object_ptr<Ui::InputField> _search;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _chooseFromUser;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _jumpToDate;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr< Ui::FadeWrapScaled<Ui::IconButton>> _lockUnlock;

	std::unique_ptr<Ui::MoreChatsBar> _moreChatsBar;

	std::unique_ptr<Ui::PlainShadow> _forumTopShadow;
	std::unique_ptr<Ui::GroupCallBar> _forumGroupCallBar;
	std::unique_ptr<Ui::RequestsBar> _forumRequestsBar;
	std::unique_ptr<HistoryView::ContactStatus> _forumReportBar;

	object_ptr<Ui::ElasticScroll> _scroll;
	QPointer<InnerWidget> _inner;
	std::unique_ptr<Suggestions> _suggestions;
	std::vector<std::unique_ptr<Suggestions>> _hidingSuggestions;
	class BottomButton;
	object_ptr<BottomButton> _updateTelegram = { nullptr };
	object_ptr<BottomButton> _loadMoreChats = { nullptr };
	std::unique_ptr<Ui::DownloadBar> _downloadBar;
	std::unique_ptr<Window::ConnectionState> _connecting;

	Ui::Animations::Simple _scrollToAnimation;
	int _scrollAnimationTo = 0;
	std::unique_ptr<Window::SlideAnimation> _showAnimation;
	rpl::variable<float64> _shownProgressValue;

	Ui::Animations::Simple _scrollToTopShown;
	object_ptr<Ui::JumpDownButton> _scrollToTop;
	bool _scrollToTopIsShown = false;
	bool _forumSearchRequested = false;
	bool _searchingHashtag = false;

	Data::Folder *_openedFolder = nullptr;
	Data::Forum *_openedForum = nullptr;
	SearchState _searchState;
	History *_searchInMigrated = nullptr;
	rpl::lifetime _searchTagsLifetime;
	QString _lastSearchText;
	bool _searchSuggestionsLocked = false;
	bool _searchHasFocus = false;
	bool _processingSearch = false;

	rpl::event_stream<rpl::producer<Stories::Content>> _storiesContents;
	base::flat_map<PeerId, Ui::PeerUserpicView> _storiesUserpicsViewsHidden;
	base::flat_map<PeerId, Ui::PeerUserpicView> _storiesUserpicsViewsShown;
	Fn<void()> _updateScrollGeometryCached;
	std::unique_ptr<Stories::List> _stories;
	Ui::Animations::Simple _storiesExplicitExpandAnimation;
	rpl::variable<int> _storiesExplicitExpandValue = 0;
	int _storiesExplicitExpandScrollTop = 0;
	int _aboveScrollAdded = 0;
	bool _storiesExplicitExpand = false;
	bool _postponeProcessSearchFocusChange = false;

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
	std::vector<Data::ReactionId> _searchQueryTags;
	ChatSearchTab _searchQueryTab = {};
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

	std::unique_ptr<Widget> _childList;
	std::unique_ptr<Ui::RpWidget> _childListShadow;
	rpl::variable<float64> _childListShown;
	rpl::variable<PeerId> _childListPeerId;
	std::unique_ptr<Ui::RpWidget> _hideChildListCanvas;

};

} // namespace Dialogs
