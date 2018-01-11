/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

enum HistoryCursorState {
	HistoryDefaultCursorState,
	HistoryInTextCursorState,
	HistoryInDateCursorState,
	HistoryInForwardedCursorState,
};

struct HistoryTextState {
	HistoryTextState() = default;
	HistoryTextState(not_null<const HistoryItem*> item);
	HistoryTextState(
		not_null<const HistoryItem*> item,
		const Text::StateResult &state);
	HistoryTextState(
		not_null<const HistoryItem*> item,
		ClickHandlerPtr link);
	HistoryTextState(
		std::nullptr_t,
		const Text::StateResult &state)
	: cursor(state.uponSymbol
		? HistoryInTextCursorState
		: HistoryDefaultCursorState)
	, link(state.link)
	, afterSymbol(state.afterSymbol)
	, symbol(state.symbol) {
	}
	HistoryTextState(std::nullptr_t, ClickHandlerPtr link)
	: link(link) {
	}

	FullMsgId itemId;
	HistoryCursorState cursor = HistoryDefaultCursorState;
	ClickHandlerPtr link;
	bool afterSymbol = false;
	uint16 symbol = 0;

};

struct HistoryStateRequest {
	Text::StateRequest::Flags flags = Text::StateRequest::Flag::LookupLink;
	Text::StateRequest forText() const {
		Text::StateRequest result;
		result.flags = flags;
		return result;
	}
};

enum InfoDisplayType {
	InfoDisplayDefault,
	InfoDisplayOverImage,
	InfoDisplayOverBackground,
};
