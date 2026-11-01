/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item_text.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace HistoryView {

struct CopySelectionArgs {
	std::shared_ptr<Ui::Show> show;
	not_null<Main::Session*> session;
	HistorySelectedTextParts text;
	Iv::RichPageBlocksSlice blocks;
	bool withImageText = false;
};

void CopySelection(CopySelectionArgs &&args);

[[nodiscard]] bool IsCopyWithImageTextShortcut(
	not_null<QKeyEvent*> e,
	not_null<Main::Session*> session);

} // namespace HistoryView
