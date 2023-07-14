/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Window {

enum class SectionActionResult {
	Handle, // Handle an action and stay in the current section.
	Fallback, // Ignore an action and fallback to the HistoryWidget.
	Ignore, // Ignore an action and stay in the current section.
};

} // namespace Window
