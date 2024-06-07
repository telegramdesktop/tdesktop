/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_channel_earn.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

namespace Ui {
class Show;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info::ChannelEarn {

class Memento;

[[nodiscard]] QImage IconCurrency(
	const style::FlatLabel &label,
	const QColor &c);

class InnerWidget final : public Ui::VerticalLayout {
public:
	struct ShowRequest final {
	};

	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const;

	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	[[nodiscard]] rpl::producer<ShowRequest> showRequests() const;

	void showFinished();
	void setInnerFocus();

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	void load();
	void fill();

	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	std::shared_ptr<Ui::Show> _show;

	Data::EarnStatistics _state;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<ShowRequest> _showRequests;
	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _focusRequested;
	rpl::event_stream<bool> _loaded;

};

} // namespace Info::ChannelEarn
