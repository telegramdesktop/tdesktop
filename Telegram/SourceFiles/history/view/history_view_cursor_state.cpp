/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_cursor_state.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"

namespace HistoryView {

TextState::TextState(not_null<const HistoryItem*> item)
: itemId(item->fullId()) {
}

TextState::TextState(
	not_null<const HistoryItem*> item,
	const Ui::Text::StateResult &state)
: itemId(item->fullId())
, cursor(state.uponSymbol
	? CursorState::Text
	: CursorState::None)
, link(state.link)
, symbol(state.symbol)
, afterSymbol(state.afterSymbol)
, selectionCursor(MessageSelectionEndpoint::Flat({
	state.symbol,
	state.afterSymbol,
})) {
}

TextState::TextState(
	not_null<const HistoryItem*> item,
	ClickHandlerPtr link)
: itemId(item->fullId())
, link(link) {
}

TextState::TextState(
	not_null<const HistoryView::Element*> view)
: TextState(view->data()) {
}

TextState::TextState(
	not_null<const HistoryView::Element*> view,
	const Ui::Text::StateResult &state)
: TextState(view->data(), state) {
}

TextState::TextState(
	not_null<const HistoryView::Element*> view,
	ClickHandlerPtr link)
: TextState(view->data(), link) {
}

TextState::TextState(
	std::nullptr_t,
	const Ui::Text::StateResult &state)
: cursor(state.uponSymbol
	? CursorState::Text
	: CursorState::None)
, link(state.link)
, symbol(state.symbol)
, afterSymbol(state.afterSymbol)
, selectionCursor(MessageSelectionEndpoint::Flat({
	state.symbol,
	state.afterSymbol,
})) {
}

TextState::TextState(std::nullptr_t, ClickHandlerPtr link)
: link(link) {
}

void SyncFlatSelectionCursor(not_null<TextState*> state) {
	if (!state->selectionCursor.isRichPage()) {
		state->selectionCursor = MessageSelectionEndpoint::Flat({
			state->symbol,
			state->afterSymbol,
		});
	}
}

void SetTextStatePosition(
		not_null<TextState*> state,
		uint16 symbol,
		bool afterSymbol) {
	state->symbol = symbol;
	state->afterSymbol = afterSymbol;
	SyncFlatSelectionCursor(state);
}

void AddTextStateOffset(not_null<TextState*> state, uint16 offset) {
	state->symbol = uint16(state->symbol + offset);
	SyncFlatSelectionCursor(state);
}

not_null<HistoryItem*> LookupItemByPoint(
		not_null<Element*> view,
		QPoint itemPoint) {
	if (view->pointState(itemPoint) == PointState::GroupPart) {
		const auto state = view->textState(itemPoint, {});
		const auto &owner = view->data()->history()->owner();
		if (const auto item = owner.message(state.itemId)) {
			return item;
		}
	}
	return view->data();
}

} // namespace HistoryView
