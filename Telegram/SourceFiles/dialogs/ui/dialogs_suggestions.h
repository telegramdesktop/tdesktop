/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "dialogs/ui/top_peers_strip.h"
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

	void selectSkip(int delta);
	void selectSkipPage(int height, int direction);
	void selectLeft();
	void selectRight();
	void chooseRow();

	[[nodiscard]] rpl::producer<not_null<PeerData*>> topPeerChosen() const {
		return _topPeerChosen.events();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> recentPeerChosen() const {
		return _recentPeerChosen.events();
	}

private:
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
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _recentPeers;
	const not_null<Ui::SlideWrap<Ui::RpWidget>*> _emptyRecent;

	rpl::event_stream<not_null<PeerData*>> _topPeerChosen;
	rpl::event_stream<not_null<PeerData*>> _recentPeerChosen;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

[[nodiscard]] RecentPeersList RecentPeersContent(
	not_null<Main::Session*> session);

} // namespace Dialogs
