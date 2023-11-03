/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "info/statistics/info_statistics_common.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

namespace Info {
class Controller;
} // namespace Info

namespace Info::Statistics {

class Memento;
class MessagePreview;

void FillLoading(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<bool> toggleOn,
	rpl::producer<> showFinished);

class InnerWidget final : public Ui::VerticalLayout {
public:
	struct ShowRequest final {
		PeerId info = PeerId(0);
		FullMsgId history;
		FullMsgId messageStatistic;
	};

	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		FullMsgId contextId);

	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] FullMsgId contextId() const;

	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	[[nodiscard]] rpl::producer<ShowRequest> showRequests() const;

	void showFinished();

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	void load();
	void fill();
	void fillRecentPosts();

	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	FullMsgId _contextId;

	std::vector<not_null<MessagePreview*>> _messagePreviews;

	SavedState _state;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<ShowRequest> _showRequests;
	rpl::event_stream<> _showFinished;
	rpl::event_stream<bool> _loaded;

};

} // namespace Info::Statistics
