/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_credits_earn.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

namespace Ui {
class Show;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info::BotEarn {

class Memento;

[[nodiscard]] QImage IconCurrency(
	const style::FlatLabel &label,
	const QColor &c);

class InnerWidget final : public Ui::VerticalLayout {
public:
	struct ShowRequest final {
	};

	InnerWidget(QWidget *parent, not_null<Controller*> controller);

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
	void fillHistory();

	not_null<Controller*> _controller;
	std::shared_ptr<Ui::Show> _show;

	Data::CreditsEarnStatistics _state;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<ShowRequest> _showRequests;
	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _focusRequested;
	rpl::event_stream<bool> _loaded;
	rpl::event_stream<> _stateUpdated;

};

} // namespace Info::BotEarn
