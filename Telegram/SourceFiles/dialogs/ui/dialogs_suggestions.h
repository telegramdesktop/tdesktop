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

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
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
		return _recentPeerChosen.events();
	}
	[[nodiscard]] auto myChannelChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _myChannelChosen.events();
	}
	[[nodiscard]] auto recommendationChosen() const
	-> rpl::producer<not_null<PeerData*>> {
		return _recommendationChosen.events();
	}

private:
	enum class Tab : uchar {
		Chats,
		Channels,
	};
	enum class JumpResult : uchar {
		NotApplied,
		Applied,
		AppliedAndOut,
	};

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setupTabs();
	void setupChats();
	void setupChannels();

	void selectJumpChats(Qt::Key direction, int pageSize);
	void selectJumpChannels(Qt::Key direction, int pageSize);

	[[nodiscard]] Data::Thread *updateFromChatsDrag(QPoint globalPosition);
	[[nodiscard]] Data::Thread *updateFromChannelsDrag(
		QPoint globalPosition);
	[[nodiscard]] Data::Thread *fromListId(uint64 peerListRowId);

	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupRecentPeers(
		RecentPeersList recentPeers);
	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupEmptyRecent();
	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupMyChannels();
	[[nodiscard]] auto setupRecommendations()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;
	[[nodiscard]] auto setupEmptyChannels()
		-> object_ptr<Ui::SlideWrap<Ui::RpWidget>>;
	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupEmpty(
		not_null<QWidget*> parent,
		SearchEmptyIcon icon,
		rpl::producer<QString> text);

	void switchTab(Tab tab);
	void startShownAnimation(bool shown, Fn<void()> finish);
	void startSlideAnimation();
	void finishShow();

	void handlePressForChatPreview(PeerId id, Fn<void(bool)> callback);

	const not_null<Window::SessionController*> _controller;

	const std::unique_ptr<Ui::SettingsSlider> _tabs;
	rpl::variable<Tab> _tab = Tab::Chats;

	const std::unique_ptr<Ui::ElasticScroll> _chatsScroll;
	const not_null<Ui::VerticalLayout*> _chatsContent;
	const not_null<Ui::SlideWrap<TopPeersStrip>*> _topPeersWrap;
	const not_null<TopPeersStrip*> _topPeers;

	rpl::variable<int> _recentCount;
	Fn<bool()> _recentPeersChoose;
	Fn<JumpResult(Qt::Key, int)> _recentSelectJump;
	Fn<uint64(QPoint)> _recentUpdateFromParentDrag;
	Fn<void()> _recentDragLeft;
	Fn<bool(not_null<QTouchEvent*>)> _recentProcessTouch;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _recentPeers;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyRecent;

	const std::unique_ptr<Ui::ElasticScroll> _channelsScroll;
	const not_null<Ui::VerticalLayout*> _channelsContent;

	rpl::variable<int> _myChannelsCount;
	Fn<bool()> _myChannelsChoose;
	Fn<JumpResult(Qt::Key, int)> _myChannelsSelectJump;
	Fn<uint64(QPoint)> _myChannelsUpdateFromParentDrag;
	Fn<void()> _myChannelsDragLeft;
	Fn<bool(not_null<QTouchEvent*>)> _myChannelsProcessTouch;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _myChannels;

	rpl::variable<int> _recommendationsCount;
	Fn<bool()> _recommendationsChoose;
	Fn<JumpResult(Qt::Key, int)> _recommendationsSelectJump;
	Fn<uint64(QPoint)> _recommendationsUpdateFromParentDrag;
	Fn<void()> _recommendationsDragLeft;
	Fn<bool(not_null<QTouchEvent*>)> _recommendationsProcessTouch;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _recommendations;

	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyChannels;

	rpl::event_stream<not_null<PeerData*>> _topPeerChosen;
	rpl::event_stream<not_null<PeerData*>> _recentPeerChosen;
	rpl::event_stream<not_null<PeerData*>> _myChannelChosen;
	rpl::event_stream<not_null<PeerData*>> _recommendationChosen;

	Ui::Animations::Simple _shownAnimation;
	Fn<void()> _showFinished;
	bool _hidden = false;
	bool _persist = false;
	QPixmap _cache;

	Ui::Animations::Simple _slideAnimation;
	QPixmap _slideLeft;
	QPixmap _slideRight;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] RecentPeersList RecentPeersContent(
	not_null<Main::Session*> session);

} // namespace Dialogs
