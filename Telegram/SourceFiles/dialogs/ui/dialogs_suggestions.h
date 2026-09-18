/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/timer.h"
#include "dialogs/ui/top_peers_strip.h"
#include "ui/controls/swipe_handler_data.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class PeerListContent;

namespace Api {
struct GlobalMediaResult;
class PeerSearch;
} // namespace Api

namespace Data {
class Thread;
} // namespace Data

namespace Info {
class WrapWidget;
} // namespace Info

namespace Main {
class Session;
} // namespace Main

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Ui::Controls {
struct SwipeHandlerArgs;
struct SwipeHandlerFinishData;
} // namespace Ui::Controls

namespace Ui {
class BoxContent;
class SearchFieldController;
class ScrollArea;
class ElasticScroll;
class SettingsSlider;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

class InnerWidget;
struct ChosenRow;
class PostsSearch;
class PostsSearchIntro;
struct PostsSearchIntroState;
enum class SearchEmptyIcon;

struct RecentPeersList {
	std::vector<not_null<PeerData*>> list;
};

class Suggestions final : public Ui::RpWidget {
public:
	Suggestions(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<TopPeersList> topPeers,
		RecentPeersList recentPeers);
	~Suggestions();

	void selectJump(Qt::Key direction, int pageSize = 0);
	void chooseRow();

	bool consumeSearchQuery(const QString &query);
	[[nodiscard]] bool ownsSearchQuery(const QString &query) const;
	[[nodiscard]] rpl::producer<> clearSearchQueryRequests() const;
	[[nodiscard]] rpl::producer<> reapplySearchQueryRequests() const;

	[[nodiscard]] Data::Thread *updateFromParentDrag(QPoint globalPosition);
	void dragLeft();

	void show(anim::type animated, Fn<void()> finish);
	void hide(anim::type animated, Fn<void()> finish);
	[[nodiscard]] float64 shownOpacity() const;

	[[nodiscard]] bool persist() const;
	void clearPersistance();

	[[nodiscard]] bool chatsTabActive() const;
	void setTabsOnly(bool tabsOnly);
	[[nodiscard]] bool tabsOnly() const;
	[[nodiscard]] int tabsHeight() const;

	[[nodiscard]] rpl::producer<not_null<PeerData*>> topPeerChosen() const {
		return _topPeerChosen.events();
	}
	[[nodiscard]] auto recentPeerChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _recent->chosen.events();
	}
	[[nodiscard]] auto myChannelChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _myChannels->chosen.events();
	}
	[[nodiscard]] auto recommendationChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _recommendations->chosen.events();
	}
	[[nodiscard]] auto globalChannelChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _globalChannels->chosen.events();
	}
	[[nodiscard]] auto recentAppChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _recentApps->chosen.events();
	}
	[[nodiscard]] auto popularAppChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _popularApps->chosen.events();
	}
	[[nodiscard]] auto globalAppChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _globalApps->chosen.events();
	}
	[[nodiscard]] auto openBotMainAppRequests() const
	-> rpl::producer<not_null<PeerData*>> {
		return _openBotMainAppRequests.events();
	}
	[[nodiscard]] rpl::producer<> closeRequests() const {
		return _closeRequests.events();
	}

	class ObjectListController;

private:
	using MediaType = Storage::SharedMediaType;
	enum class Tab : uchar {
		Chats,
		Channels,
		Apps,
		Posts,
		Media,
		Downloads,
	};
	enum class JumpResult : uchar {
		NotApplied,
		Applied,
		AppliedAndOut,
	};

	struct Key {
		Tab tab = Tab::Chats;
		MediaType mediaType = {};

		friend inline auto operator<=>(Key, Key) = default;
		friend inline bool operator==(Key, Key) = default;
	};

	struct ObjectList {
		not_null<Ui::SlideWrap<PeerListContent>*> wrap;
		rpl::variable<int> count;
		Fn<bool()> choose;
		Fn<JumpResult(Qt::Key, int)> selectJump;
		Fn<uint64(QPoint)> updateFromParentDrag;
		Fn<void()> dragLeft;
		Fn<bool(not_null<QTouchEvent*>)> processTouch;
		rpl::event_stream<not_null<PeerData*>> chosen;
	};

	struct MediaList {
		Info::WrapWidget *wrap = nullptr;
		rpl::variable<int> count;
	};
	struct SearchList;

	[[nodiscard]] static std::vector<Key> TabKeysFor(
		not_null<Window::SessionController*> controller);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setupTabs();
	void setupChats();
	void setupChannels();
	void setupApps();
	void reinstallSwipe(not_null<Ui::ElasticScroll*>);
	[[nodiscard]] auto generateIncompleteSwipeArgs()
	-> Ui::Controls::SwipeHandlerArgs;

	void selectJumpChats(Qt::Key direction, int pageSize);
	void selectJumpChannels(Qt::Key direction, int pageSize);
	void selectJumpApps(Qt::Key direction, int pageSize);
	void selectJumpSections(
		const std::vector<Fn<JumpResult(Qt::Key, int)>> &sections,
		not_null<Ui::ElasticScroll*> scroll,
		Qt::Key direction,
		int pageSize);

	[[nodiscard]] Data::Thread *updateFromChatsDrag(QPoint globalPosition);
	[[nodiscard]] Data::Thread *updateFromChannelsDrag(
		QPoint globalPosition);
	[[nodiscard]] Data::Thread *updateFromAppsDrag(QPoint globalPosition);
	[[nodiscard]] Data::Thread *fromListId(uint64 peerListRowId);
	[[nodiscard]] not_null<ObjectList*> channelsSecondList() const;
	[[nodiscard]] not_null<ObjectList*> appsSecondList() const;
	[[nodiscard]] Ui::SearchFieldController *mediaListSearch(Key key) const;


	[[nodiscard]] std::unique_ptr<ObjectList> setupRecentPeers(
		RecentPeersList recentPeers);
	[[nodiscard]] auto setupEmptyRecent()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;

	[[nodiscard]] std::unique_ptr<ObjectList> setupMyChannels();
	[[nodiscard]] std::unique_ptr<ObjectList> setupRecommendations();
	[[nodiscard]] std::unique_ptr<SearchList> setupChannelsPosts();
	[[nodiscard]] auto setupEmptyChannels()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;

	[[nodiscard]] std::unique_ptr<ObjectList> setupRecentApps();
	[[nodiscard]] std::unique_ptr<ObjectList> setupPopularApps();

	[[nodiscard]] static bool TakesSearchQuery(Key key);
	[[nodiscard]] static bool ListsSearchResults(Key key);
	[[nodiscard]] static auto ListSelectJump(not_null<ObjectList*> raw)
		-> Fn<JumpResult(Qt::Key, int)>;
	[[nodiscard]] std::unique_ptr<ObjectList> setupObjectList(
		not_null<Ui::ElasticScroll*> scroll,
		not_null<Ui::VerticalLayout*> parent,
		not_null<ObjectListController*> controller,
		Fn<int()> addToScroll = nullptr);
	[[nodiscard]] std::unique_ptr<ObjectList> setupGlobalPeers(
		not_null<Ui::ElasticScroll*> scroll,
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> peers,
		not_null<ObjectList*> above,
		bool expandable);

	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupEmpty(
		not_null<QWidget*> parent,
		not_null<Ui::ElasticScroll*> scroll,
		SearchEmptyIcon icon,
		rpl::producer<TextWithEntities> text);

	bool setTabSearchQuery(const QString &query);
	void resetTabSearchQuery(Key key);
	void switchTab(Key key);
	void startShownAnimation(bool shown, Fn<void()> finish);
	void startSlideAnimation(Key was, Key now);
	void ensureContent(Key key);
	void finishShow();

	void handlePressForChatPreview(PeerId id, Fn<void(bool)> callback);
	void updateControlsGeometry();
	void applySearchQuery();

	void setupPostsSearch();
	void setPostsSearchQuery(const QString &query);
	void setupPostsResults();
	void setupPostsIntro(const PostsSearchIntroState &intro);
	void updatePostsSearchVisibleRange();
	void showSearchResult(const ChosenRow &row, const QString &query);

	[[nodiscard]] std::unique_ptr<SearchList> setupSearchList(Key key);
	void setupSearchListContent(not_null<SearchList*> search);
	[[nodiscard]] SearchList *shownSearchList(Key key) const;
	void setSearchListQuery(Key key, const QString &query);
	void resetSearchList(not_null<SearchList*> search, const QString &query);
	void setChannelsSearchQuery(const QString &query);
	void requestChannelsSearch();
	void setAppsSearchQuery(const QString &query);
	void requestAppsSearch();
	void requestSearchList(not_null<SearchList*> search);
	void searchListReceived(
		not_null<SearchList*> search,
		const Api::GlobalMediaResult &result);
	void updateChannelsPostsVisibleRange();
	void updateSearchListVisibleRange(not_null<SearchList*> search);

	const not_null<Window::SessionController*> _controller;

	const std::unique_ptr<Ui::ScrollArea> _tabsScroll;
	const not_null<Ui::SettingsSlider*> _tabs;
	Ui::Animations::Simple _tabsScrollAnimation;
	const std::vector<Key> _tabKeys;
	rpl::variable<Key> _key;

	const std::unique_ptr<Ui::ElasticScroll> _chatsScroll;
	const not_null<Ui::VerticalLayout*> _chatsContent;

	const not_null<Ui::SlideWrap<TopPeersStrip>*> _topPeersWrap;
	const not_null<TopPeersStrip*> _topPeers;
	rpl::event_stream<not_null<PeerData*>> _topPeerChosen;
	rpl::event_stream<not_null<PeerData*>> _openBotMainAppRequests;
	rpl::event_stream<> _closeRequests;

	const std::unique_ptr<ObjectList> _recent;

	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyRecent;

	const std::unique_ptr<Ui::ElasticScroll> _channelsScroll;
	const not_null<Ui::VerticalLayout*> _channelsContent;
	rpl::variable<QString> _channelsQuery;
	rpl::variable<std::vector<not_null<PeerData*>>> _joinedChannelsResults;
	rpl::variable<std::vector<not_null<PeerData*>>> _globalChannelsResults;
	rpl::variable<bool> _channelsLoading = false;
	rpl::variable<bool> _channelsHasPosts = false;
	bool _channelsPostsKeyJump = false;
	std::unique_ptr<Api::PeerSearch> _channelsPeerSearch;

	const std::unique_ptr<ObjectList> _myChannels;
	const std::unique_ptr<ObjectList> _recommendations;
	const std::unique_ptr<ObjectList> _globalChannels;
	const std::unique_ptr<SearchList> _channelsPosts;

	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyChannels;

	const std::unique_ptr<Ui::ElasticScroll> _appsScroll;
	const not_null<Ui::VerticalLayout*> _appsContent;

	std::unique_ptr<PostsSearch> _postsSearch;
	const std::unique_ptr<Ui::ElasticScroll> _postsScroll;
	const not_null<Ui::RpWidget*> _postsWrap;
	PostsSearchIntro *_postsSearchIntro = nullptr;
	InnerWidget *_postsContent = nullptr;

	rpl::variable<QString> _appsQuery;
	rpl::variable<std::vector<not_null<PeerData*>>> _usedAppsResults;
	rpl::variable<std::vector<not_null<PeerData*>>> _globalAppsResults;
	rpl::variable<bool> _appsLoading = false;
	std::unique_ptr<Api::PeerSearch> _appsPeerSearch;
	rpl::producer<> _recentAppsRefreshed;
	Fn<bool(not_null<PeerData*>)> _recentAppsShows;
	const std::unique_ptr<ObjectList> _recentApps;
	const std::unique_ptr<ObjectList> _popularApps;
	const std::unique_ptr<ObjectList> _globalApps;

	base::flat_map<Key, MediaList> _mediaLists;
	base::flat_map<Key, std::unique_ptr<SearchList>> _searchLists;
	rpl::event_stream<> _clearSearchQueryRequests;
	rpl::event_stream<> _reapplySearchQueryRequests;
	QString _fieldQuery;
	QString _searchQuery;
	QString _postsSearchQuery;
	base::Timer _searchQueryTimer;

	Ui::Animations::Simple _shownAnimation;
	Fn<void()> _showFinished;
	bool _hidden = false;
	bool _persist = false;
	bool _tabsOnly = false;
	QPixmap _cache;

	Ui::Animations::Simple _slideAnimation;
	QPixmap _slideLeft;
	QPixmap _slideRight;

	Ui::Controls::SwipeBackResult _swipeBackData;
	rpl::lifetime _swipeLifetime;

	int _slideLeftTop = 0;
	int _slideRightTop = 0;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] RecentPeersList RecentPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] object_ptr<Ui::BoxContent> StarsExamplesBox(
	not_null<Window::SessionController*> window);

[[nodiscard]] object_ptr<Ui::BoxContent> PopularAppsAboutBox(
	not_null<Window::SessionController*> window);

} // namespace Dialogs
