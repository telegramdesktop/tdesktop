/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {

[[nodiscard]] base::unique_qptr<Ui::PopupMenu> MakeChatPreview(
	QWidget *parent,
	not_null<Dialogs::Entry*> entry);

} // namespace HistoryView
