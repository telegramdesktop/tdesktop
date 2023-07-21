/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace HistoryView {

enum class DoubleClickQuickAction {
	Reply, // Default.
	React,
	None,
};

[[nodiscard]] DoubleClickQuickAction CurrentQuickAction();

} // namespace HistoryView
