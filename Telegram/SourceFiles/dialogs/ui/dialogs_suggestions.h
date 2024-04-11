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

class Suggestions final : public Ui::RpWidget {
public:
	Suggestions(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<TopPeersList> topPeers);
	~Suggestions();

	[[nodiscard]] rpl::producer<PeerId> topPeerChosen() const {
		return _topPeerChosen.events();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	[[nodiscard]] object_ptr<Ui::RpWidget> setupDivider();

	void updateControlsGeometry();

	const std::unique_ptr<Ui::ElasticScroll> _scroll;
	const not_null<Ui::VerticalLayout*> _content;
	const not_null<Ui::SlideWrap<TopPeersStrip>*> _topPeersWrap;
	const not_null<TopPeersStrip*> _topPeers;
	const not_null<Ui::RpWidget*> _divider;

	rpl::event_stream<PeerId> _topPeerChosen;

};

[[nodiscard]] rpl::producer<TopPeersList> TopPeersContent(
	not_null<Main::Session*> session);

} // namespace Dialogs
