/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_keyboard_text_selection.h"

#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "ui/text/text.h"

namespace HistoryView {

bool KeyboardTextSelection::IsExtendKey(int key) {
	return (key == Qt::Key_Left)
		|| (key == Qt::Key_Right)
		|| (key == Qt::Key_Home)
		|| (key == Qt::Key_End);
}

std::optional<MessageSelection> KeyboardTextSelection::extend(
		not_null<Element*> view,
		const MessageSelection &current,
		int key,
		Qt::KeyboardModifiers modifiers) {
	if (!IsExtendKey(key)) {
		return std::nullopt;
	}
	const auto maxOffset = int(view->adjustSelection(
		TextSelection(0, 0xFFFF),
		TextSelectType::Letters).to);
	if (maxOffset >= 0xFFFF) {
		return std::nullopt;
	}
	const auto item = view->data().get();
	const auto continuing = _has
		&& (_item == item)
		&& (current.flatSelection() == _produced);
	if (!continuing) {
		if (!current.isFlat()) {
			return std::nullopt;
		}
		const auto flat = current.flat;
		const auto rawAnchor = current.anchor.isFlat()
			? current.anchor.flat.offset()
			: flat.from;
		const auto rawFocus = current.focus.isFlat()
			? current.focus.flat.offset()
			: flat.to;
		const auto focusAtEnd = (rawFocus >= rawAnchor);
		_anchor = { focusAtEnd ? flat.from : flat.to, false };
		_focus = { focusAtEnd ? flat.to : flat.from, false };
	}

	const auto position = int(_focus.offset());
	const auto forward = (key == Qt::Key_Right);
#ifdef Q_OS_MAC
	const auto byWord = (modifiers & Qt::AltModifier) != 0;
#else // Q_OS_MAC
	const auto byWord = (modifiers & Qt::ControlModifier) != 0;
#endif // Q_OS_MAC
	auto wanted = position;
	if (key == Qt::Key_Home) {
		wanted = 0;
	} else if (key == Qt::Key_End) {
		wanted = maxOffset;
	} else if (byWord) {
		const auto separator = [&](int symbol) {
			const auto one = view->selectedText(
				TextSelection(uint16(symbol), uint16(symbol + 1))).rich.text;
			return one.isEmpty() || Ui::Text::IsWordSeparator(one[0]);
		};
		auto symbol = position;
		if (forward) {
			while (symbol < maxOffset && separator(symbol)) {
				++symbol;
			}
			while (symbol < maxOffset && !separator(symbol)) {
				++symbol;
			}
		} else {
			if (symbol > 0) {
				--symbol;
			}
			while (symbol > 0 && separator(symbol)) {
				--symbol;
			}
			while (symbol > 0 && !separator(symbol - 1)) {
				--symbol;
			}
		}
		wanted = symbol;
	} else {
		wanted = position + (forward ? 1 : -1);
	}
	_focus = { uint16(std::clamp(wanted, 0, maxOffset)), false };

	auto result = MessageSelection::Flat(_anchor, _focus);
	_has = true;
	_item = item;
	_produced = result.flatSelection();
	return result;
}

} // namespace HistoryView
