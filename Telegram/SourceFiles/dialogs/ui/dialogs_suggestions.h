/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "dialogs/ui/top_peers_strip.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class PeerListContent;

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class BoxContent;
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

	[[nodiscard]] Data::Thread *updateFromParentDrag(QPoint globalPosition);
	void dragLeft();

	void show(anim::type animated, Fn<void()> finish);
	void hide(anim::type animated, Fn<void()> finish);
	[[nodiscard]] float64 shownOpacity() const;

	[[nodiscard]] bool persist() const;
	void clearPersistance();

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
	[[nodiscard]] auto recentAppChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _recentApps->chosen.events();
	}
	[[nodiscard]] auto popularAppChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _popularApps->chosen.events();
	}

	class ObjectListController;

private:
	enum class Tab : uchar {
		Chats,
		Channels,
		Apps,
	};
	enum class JumpResult : uchar {
		NotApplied,
		Applied,
		AppliedAndOut,
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

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setupTabs();
	void setupChats();
	void setupChannels();
	void setupApps();

	void selectJumpChats(Qt::Key direction, int pageSize);
	void selectJumpChannels(Qt::Key direction, int pageSize);
	void selectJumpApps(Qt::Key direction, int pageSize);

	[[nodiscard]] Data::Thread *updateFromChatsDrag(QPoint globalPosition);
	[[nodiscard]] Data::Thread *updateFromChannelsDrag(
		QPoint globalPosition);
	[[nodiscard]] Data::Thread *updateFromAppsDrag(QPoint globalPosition);
	[[nodiscard]] Data::Thread *fromListId(uint64 peerListRowId);

	[[nodiscard]] std::unique_ptr<ObjectList> setupRecentPeers(
		RecentPeersList recentPeers);
	[[nodiscard]] auto setupEmptyRecent()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;

	[[nodiscard]] std::unique_ptr<ObjectList> setupMyChannels();
	[[nodiscard]] std::unique_ptr<ObjectList> setupRecommendations();
	[[nodiscard]] auto setupEmptyChannels()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;

	[[nodiscard]] std::unique_ptr<ObjectList> setupRecentApps();
	[[nodiscard]] std::unique_ptr<ObjectList> setupPopularApps();

	[[nodiscard]] std::unique_ptr<ObjectList> setupObjectList(
		not_null<Ui::ElasticScroll*> scroll,
		not_null<Ui::VerticalLayout*> parent,
		not_null<ObjectListController*> controller,
		Fn<int()> addToScroll = nullptr);

	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupEmpty(
		not_null<QWidget*> parent,
		SearchEmptyIcon icon,
		rpl::producer<QString> text);

	void switchTab(Tab tab);
	void startShownAnimation(bool shown, Fn<void()> finish);
	void startSlideAnimation(Tab was, Tab now);
	void finishShow();

	void handlePressForChatPreview(PeerId id, Fn<void(bool)> callback);

	const not_null<Window::SessionController*> _controller;

	const std::unique_ptr<Ui::SettingsSlider> _tabs;
	rpl::variable<Tab> _tab = Tab::Chats;

	const std::unique_ptr<Ui::ElasticScroll> _chatsScroll;
	const not_null<Ui::VerticalLayout*> _chatsContent;

	const not_null<Ui::SlideWrap<TopPeersStrip>*> _topPeersWrap;
	const not_null<TopPeersStrip*> _topPeers;
	rpl::event_stream<not_null<PeerData*>> _topPeerChosen;

	const std::unique_ptr<ObjectList> _recent;

	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyRecent;

	const std::unique_ptr<Ui::ElasticScroll> _channelsScroll;
	const not_null<Ui::VerticalLayout*> _channelsContent;

	const std::unique_ptr<ObjectList> _myChannels;
	const std::unique_ptr<ObjectList> _recommendations;

	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyChannels;

	const std::unique_ptr<Ui::ElasticScroll> _appsScroll;
	const not_null<Ui::VerticalLayout*> _appsContent;

	rpl::producer<> _recentAppsRefreshed;
	Fn<bool(not_null<PeerData*>)> _recentAppsShows;
	const std::unique_ptr<ObjectList> _recentApps;
	const std::unique_ptr<ObjectList> _popularApps;

	Ui::Animations::Simple _shownAnimation;
	Fn<void()> _showFinished;
	bool _hidden = false;
	bool _persist = false;
	QPixmap _cache;

	Ui::Animations::Simple _slideAnimation;
	QPixmap _slideLeft;
	QPixmap _slideRight;

	int _slideLeftTop = 0;
	int _slideRightTop = 0;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] RecentPeersList RecentPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] object_ptr<Ui::BoxContent> StarsExamplesBox(
	not_null<Window::SessionController*> window);

} // namespace Dialogs
