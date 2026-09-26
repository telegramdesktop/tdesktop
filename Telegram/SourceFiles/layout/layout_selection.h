/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

constexpr auto FullSelection = TextSelection { 0xFFFF, 0xFFFF };
constexpr auto kMaxGroupSelectionItems = 0x0F;

[[nodiscard]] bool IsSubGroupSelection(TextSelection selection);

[[nodiscard]] bool IsGroupItemSelection(
		TextSelection selection,
		int index);

[[nodiscard]] int FirstGroupItemIndex(TextSelection selection);

[[nodiscard]] TextSelection AddGroupItemSelection(
		TextSelection selection,
		int index);

[[nodiscard]] TextSelection RemoveGroupItemSelection(
		TextSelection selection,
		int index);
