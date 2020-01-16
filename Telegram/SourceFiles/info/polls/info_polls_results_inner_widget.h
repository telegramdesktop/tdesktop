/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"
#include "base/object_ptr.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {

class Controller;

namespace Polls {

class Memento;
class ListController;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PollData*> poll,
		FullMsgId contextId);

	[[nodiscard]] not_null<PollData*> poll() const {
		return _poll;
	}
	[[nodiscard]] FullMsgId contextId() const {
		return _contextId;
	}

	[[nodiscard]] auto scrollToRequests() const
		-> rpl::producer<Ui::ScrollToRequest>;

	[[nodiscard]] auto showPeerInfoRequests() const
		-> rpl::producer<not_null<PeerData*>>;

	[[nodiscard]] int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	void setupContent();

	not_null<Controller*> _controller;
	not_null<PollData*> _poll;
	FullMsgId _contextId;
	object_ptr<Ui::VerticalLayout> _content;
	base::flat_map<QByteArray, not_null<ListController*>> _sections;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<not_null<PeerData*>> _showPeerInfoRequests;
	rpl::variable<int> _visibleTop = 0;

};

} // namespace Polls
} // namespace Info
