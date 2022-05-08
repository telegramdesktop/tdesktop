/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Menu {

struct MenuCallback;

[[nodiscard]] MenuCallback CreateAddActionCallback(
	const base::unique_qptr<Ui::PopupMenu> &menu);

} // namespace Menu
