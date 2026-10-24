/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_selection.h"

#include "chat_helpers/compose/compose_show.h"
#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_list_widget.h"
#include "history/history_inner_widget.h"
#include "history/history_widget.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

#include "styles/style_menu_icons.h"

namespace Tgw {
namespace {

void AddCopyAction(not_null<Ui::PopupMenu*> menu, Fn<void()> copy) {
	menu->addAction(
		tr::lng_context_copy_selected(tr::now),
		std::move(copy),
		&st::menuIconCopy);
}

} // namespace

std::unique_ptr<WidgetSelection> ListSelection::Create(
		not_null<HistoryInner*> list) {
	return std::make_unique<WidgetSelection>(WidgetSelectionArgs{
		.list = list,
		.clearText = [=] { list->clearTextSelection(); },
		.changed = [=](bool restoreFocus) {
			list->_widget->updateTopBarSelection(!restoreFocus);
		},
		.show = [=] { return list->_controller->uiShow(); },
	});
}

std::unique_ptr<WidgetSelection> ListSelection::Create(
		not_null<HistoryView::ListWidget*> list) {
	return std::make_unique<WidgetSelection>(WidgetSelectionArgs{
		.list = list,
		.clearText = [=] { list->clearTextSelection(); },
		.changed = [=](bool restoreFocus) {
			if (restoreFocus && list->_selectedTextSelection.empty()) {
				list->_delegate->listWindowSetInnerFocus();
			}
		},
		.show = [=] { return list->_delegate->listUiShow(); },
	});
}

std::unique_ptr<WidgetSelection> ListSelection::Create(
		not_null<AdminLog::InnerWidget*> list) {
	return std::make_unique<WidgetSelection>(WidgetSelectionArgs{
		.list = list,
		.clearText = [=] {
			list->repaintItem(base::take(list->_selectedItem));
			list->_selectedTextSelection = HistoryView::MessageSelection();
		},
		.changed = [=](bool restoreFocus) {
			if (restoreFocus
				&& (!list->_selectedItem
					|| list->_selectedTextSelection.empty())) {
				list->_controller->widget()->setInnerFocus();
			}
		},
		.show = [=] { return list->_controller->uiShow(); },
	});
}

void ListSelection::FillMenu(not_null<HistoryInner*> list, HistoryItem *item) {
	if (item
		&& (list->_widgetSelection->item() == item)
		&& !list->hasCopyRestrictionForSelected()) {
		AddCopyAction(list->_menu.get(), [=] { list->copySelectedText(); });
	}
}

void ListSelection::FillMenu(
		not_null<AdminLog::InnerWidget*> list,
		HistoryItem *item) {
	if (item
		&& (list->_widgetSelection->item() == item)
		&& !list->_widgetSelection->copyRestricted()) {
		AddCopyAction(list->_menu.get(), [=] { list->copySelectedText(); });
	}
}

bool ListSelection::ShowRestriction(
		not_null<HistoryView::ListWidget*> list) {
	const auto item = list->_widgetSelection->item();
	return item
		&& (list->showCopyRestriction(item)
			|| list->_widgetSelection->showCopyRestriction());
}

void ListSelection::Watch(
		not_null<QWidget*> list,
		not_null<Ui::PopupMenu*> menu) {
	if (const auto history = dynamic_cast<HistoryInner*>(list.get())) {
		history->_widgetSelection->watch(menu);
	} else if (const auto section = dynamic_cast<HistoryView::ListWidget*>(
			list.get())) {
		section->_widgetSelection->watch(menu);
	}
}

} // namespace Tgw

namespace HistoryView {

bool ListWidget::canCopySelected() const {
	return hasSelectedItems()
		|| (hasSelectedText() && !_selectedTextSelection.empty())
		|| _widgetSelection->has();
}

bool ListWidget::hasWidgetSelection(
		not_null<const HistoryItem*> item) const {
	return (_widgetSelection->item() == item);
}

} // namespace HistoryView
