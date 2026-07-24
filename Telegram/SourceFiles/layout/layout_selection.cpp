/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/layout_selection.h"

bool IsSubGroupSelection(TextSelection selection) {
	return (selection.from == 0xFFFF) && (selection.to != 0xFFFF);
}

bool IsGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < kMaxGroupSelectionItems);

	return IsSubGroupSelection(selection) && (selection.to & (1 << index));
}

int FirstGroupItemIndex(TextSelection selection) {
	if (!IsSubGroupSelection(selection)) {
		return -1;
	}
	for (auto i = 0; i != kMaxGroupSelectionItems; ++i) {
		if (selection.to & (1 << i)) {
			return i;
		}
	}
	return -1;
}

TextSelection AddGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < kMaxGroupSelectionItems);

	const auto bit = uint16(1U << index);
	return TextSelection(
		0xFFFF,
		IsSubGroupSelection(selection) ? (selection.to | bit) : bit);
}

TextSelection RemoveGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < kMaxGroupSelectionItems);

	const auto bit = uint16(1U << index);
	return IsSubGroupSelection(selection)
		? TextSelection(0xFFFF, selection.to & ~bit)
		: selection;
}
