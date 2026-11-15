/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/lifetime.h>

#include "base/event_filter.h"
#include "base/timer.h"
#include "ui/abstract_button.h"

namespace HistoryView::Controls {

inline constexpr auto kBotMenuHoldDuration = crl::time(500);

class BotMenuHoldTracker final {
public:
	BotMenuHoldTracker(
		not_null<Ui::AbstractButton*> watch,
		crl::time duration,
		Fn<void()> activated);
	~BotMenuHoldTracker();

private:
	[[nodiscard]] base::EventFilterResult filter(not_null<QEvent*> event);
	void fire();

	const QPointer<Ui::AbstractButton> _watch;
	crl::time _duration = 0;
	base::Timer _timer;
	Fn<void()> _activated;

};

} // namespace HistoryView::Controls
