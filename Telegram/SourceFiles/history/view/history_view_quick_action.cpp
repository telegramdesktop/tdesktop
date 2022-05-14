/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_quick_action.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace HistoryView {

DoubleClickQuickAction CurrentQuickAction() {
	return Core::App().settings().chatQuickAction();
}

} // namespace HistoryView
