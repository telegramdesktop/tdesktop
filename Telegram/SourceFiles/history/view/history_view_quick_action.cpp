/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#include "history/view/history_view_quick_action.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace HistoryView {

DoubleClickQuickAction CurrentQuickAction() {
	return Core::App().settings().chatQuickAction();
}

} // namespace HistoryView
