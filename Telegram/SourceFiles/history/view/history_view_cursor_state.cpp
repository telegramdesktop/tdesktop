/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_cursor_state.h"

#include "history/history_item.h"

HistoryTextState::HistoryTextState(not_null<const HistoryItem*> item)
: itemId(item->fullId()) {
}

HistoryTextState::HistoryTextState(
	not_null<const HistoryItem*> item,
	const Text::StateResult &state)
: itemId(item->fullId())
, cursor(state.uponSymbol
	? HistoryInTextCursorState
	: HistoryDefaultCursorState)
, link(state.link)
, afterSymbol(state.afterSymbol)
, symbol(state.symbol) {
}

HistoryTextState::HistoryTextState(
	not_null<const HistoryItem*> item,
	ClickHandlerPtr link)
: itemId(item->fullId())
, link(link) {
}
