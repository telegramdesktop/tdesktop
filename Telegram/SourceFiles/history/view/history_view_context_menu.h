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

namespace HistoryView {

class ListWidget;
class Element;

struct ContextMenuRequest {
	ClickHandlerPtr link;
	Element *view = nullptr;
	MessageIdsList selectedItems;
	TextWithEntities selectedText;
	bool overView = false;
	bool overSelection = false;
};

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
	not_null<ListWidget*> list,
	const ContextMenuRequest &request);

} // namespace
