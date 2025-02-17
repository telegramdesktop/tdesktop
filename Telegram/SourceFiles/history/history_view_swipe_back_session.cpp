/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_swipe_back_session.h"

#include "history/history_view_swipe_data.h"
#include "history/history_view_swipe.h"
#include "history/view/history_view_list_widget.h"
#include "ui/chat/chat_style.h"
#include "window/window_session_controller.h"

namespace Window {

void SetupSwipeBackSection(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::ScrollArea*> scroll,
		not_null<HistoryView::ListWidget*> list) {
	const auto swipeBackData
		= list->lifetime().make_state<HistoryView::SwipeBackResult>();
	HistoryView::SetupSwipeHandler(parent, scroll, [=](
			HistoryView::ChatPaintGestureHorizontalData data) {
		if (data.translation > 0) {
			if (!swipeBackData->callback) {
				const auto color = [=]() -> std::pair<QColor, QColor> {
					const auto c = list->delegate()->listPreparePaintContext({
						.theme = list->delegate()->listChatTheme(),
					});
					return {
						c.st->msgServiceBg()->c,
						c.st->msgServiceFg()->c,
					};
				};
				(*swipeBackData) = HistoryView::SetupSwipeBack(parent, color);
			}
			swipeBackData->callback(data);
			return;
		} else if (swipeBackData->lifetime) {
			(*swipeBackData) = {};
		}
	}, [=](int, Qt::LayoutDirection direction) {
		if (direction != Qt::RightToLeft) {
			return HistoryView::SwipeHandlerFinishData();
		}
		return HistoryView::SwipeHandlerFinishData{
			.callback = [=] { list->controller()->showBackFromStack(); },
			.msgBareId = HistoryView::kMsgBareIdSwipeBack,
		};
	}, list->touchMaybeSelectingValue());
}

} // namespace Window
