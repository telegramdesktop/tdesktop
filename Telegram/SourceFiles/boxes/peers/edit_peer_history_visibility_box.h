/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

enum class HistoryVisibility {
	Visible,
	Hidden,
};

void EditPeerHistoryVisibilityBox(
	not_null<Ui::GenericBox*> box,
	bool isLegacy,
	Fn<void(HistoryVisibility)> savedCallback,
	HistoryVisibility historyVisibilitySavedValue);
