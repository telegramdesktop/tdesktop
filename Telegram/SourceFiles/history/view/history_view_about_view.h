/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/admin_log/history_admin_log_item.h"

namespace HistoryView {

class AboutView final : public ClickHandlerHost {
public:
	AboutView(
		not_null<History*> history,
		not_null<ElementDelegate*> delegate);

	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] Element *view() const;
	[[nodiscard]] HistoryItem *item() const;

	bool refresh();

	int top = 0;
	int height = 0;

private:
	[[nodiscard]] AdminLog::OwnedItem makeAboutBot(not_null<BotInfo*> info);
	[[nodiscard]] AdminLog::OwnedItem makePremiumRequired();

	const not_null<History*> _history;
	const not_null<ElementDelegate*> _delegate;
	AdminLog::OwnedItem _item;
	int _version = 0;

};

} // namespace HistoryView
