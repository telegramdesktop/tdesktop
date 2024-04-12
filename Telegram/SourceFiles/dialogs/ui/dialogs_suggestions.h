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

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ElasticScroll;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

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

	void show(anim::type animated, Fn<void()> finish);
	void hide(anim::type animated, Fn<void()> finish);
	[[nodiscard]] float64 shownOpacity() const;

	[[nodiscard]] rpl::producer<not_null<PeerData*>> topPeerChosen() const {
		return _topPeerChosen.events();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> recentPeerChosen() const {
		return _recentPeerChosen.events();
	}

private:
	enum class JumpResult {
		NotApplied,
		Applied,
		AppliedAndOut,
	};

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupRecentPeers(
		not_null<Window::SessionController*> window,
		RecentPeersList recentPeers);
	[[nodiscard]] object_ptr<Ui::SlideWrap<Ui::RpWidget>> setupEmptyRecent(
		not_null<Window::SessionController*> window);

	const std::unique_ptr<Ui::ElasticScroll> _scroll;
	const not_null<Ui::VerticalLayout*> _content;
	const not_null<Ui::SlideWrap<TopPeersStrip>*> _topPeersWrap;
	const not_null<TopPeersStrip*> _topPeers;

	rpl::variable<int> _recentCount;
	Fn<bool()> _recentPeersChoose;
	Fn<JumpResult(Qt::Key, int)> _recentSelectJump;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _recentPeers;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyRecent;

	rpl::event_stream<not_null<PeerData*>> _topPeerChosen;
	rpl::event_stream<not_null<PeerData*>> _recentPeerChosen;

	Ui::Animations::Simple _shownAnimation;
	Fn<void()> _showFinished;
	bool _hidden = false;
	QPixmap _cache;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] RecentPeersList RecentPeersContent(
	not_null<Main::Session*> session);

} // namespace Dialogs
