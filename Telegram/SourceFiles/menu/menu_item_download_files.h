/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

class HistoryInner;
class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {
class ListWidget;
struct SelectedItem;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

namespace Menu {

void AddDownloadFilesAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> window,
	const std::vector<HistoryView::SelectedItem> &selectedItems,
	not_null<HistoryView::ListWidget*> list);

void AddDownloadFilesAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> window,
	// From the legacy history inner widget.
	const std::map<HistoryItem*, TextSelection, std::less<>> &items,
	not_null<HistoryInner*> list);

} // namespace Menu
