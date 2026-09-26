/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_message_selection.h"

class HistoryItem;

namespace HistoryView {

class Element;

class KeyboardTextSelection final {
public:
	[[nodiscard]] static bool IsExtendKey(int key);

	[[nodiscard]] std::optional<MessageSelection> extend(
		not_null<Element*> view,
		const MessageSelection &current,
		int key,
		Qt::KeyboardModifiers modifiers);

private:
	HistoryItem *_item = nullptr;
	TextSelection _produced;
	bool _has = false;
	MessageSelectionFlatEndpoint _anchor;
	MessageSelectionFlatEndpoint _focus;

};

} // namespace HistoryView
