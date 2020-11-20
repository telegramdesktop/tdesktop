/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
struct GroupCallBarContent;
} // namespace Ui

namespace HistoryView {

class GroupCallTracker final {
public:
	GroupCallTracker(not_null<ChannelData*> channel);

	[[nodiscard]] rpl::producer<Ui::GroupCallBarContent> content() const;
	[[nodiscard]] rpl::producer<> joinClicks() const;

private:
	not_null<ChannelData*> _channel;

	rpl::event_stream<> _joinClicks;

};

} // namespace HistoryView
