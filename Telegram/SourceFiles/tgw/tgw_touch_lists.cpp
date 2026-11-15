/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_touch.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_service_message.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "lang/lang_keys.h"

#include "styles/style_chat.h"

namespace Tgw {
namespace {

using HistoryView::Element;

[[nodiscard]] int FloatingHeight() {
	return st::msgServicePadding.top()
		+ st::msgServiceFont->height
		+ st::msgServicePadding.bottom();
}

[[nodiscard]] int DateWidth(not_null<Element*> view) {
	const auto badge = view->Get<HistoryView::DateBadge>();
	const auto text = badge
		? badge->width
		: st::msgServiceFont->width(
			langDayOfMonthFull(view->dateTime().date()));
	return st::msgServicePadding.left()
		+ text
		+ st::msgServicePadding.right();
}

[[nodiscard]] bool DateInPlace(
		not_null<Element*> view,
		int itemtop,
		int top) {
	return view->displayDate()
		&& (top < itemtop + st::msgServiceMargin.top() + FloatingHeight());
}

[[nodiscard]] bool OverFloating(
		QPoint point,
		int top,
		int width,
		int available,
		bool chatWide) {
	if (point.y() < top || point.y() >= top + FloatingHeight()) {
		return false;
	}
	const auto margin = st::msgServiceMargin.left();
	const auto full = chatWide
		? std::min(available, HistoryView::WideChatWidth())
		: available;
	const auto left = margin + (full - 2 * margin - width) / 2;
	return (point.x() >= left) && (point.x() < left + width);
}

} // namespace

template <typename List>
bool ListTouch::Covered(
		not_null<List*> list,
		QPoint point,
		Fn<int(not_null<Element*> view)> available,
		Fn<bool(not_null<Element*> view)> clickable) {
	const auto height = FloatingHeight();
	const auto floating = (list->_scrollDateOpacity.value(
		list->_scrollDateShown ? 1. : 0.) > 0.);
	const auto over = [&](not_null<Element*> view, int top, int width) {
		return OverFloating(
			point,
			top,
			width,
			available(view),
			list->_isChatWide);
	};
	auto result = false;
	list->enumerateDates(Floating([&](
			not_null<Element*> view,
			int itemtop,
			int top) {
		if (top + height <= point.y()) {
			return false;
		} else if (top > point.y()) {
			return true;
		}
		result = (floating || DateInPlace(view, itemtop, top))
			&& over(view, top, DateWidth(view))
			&& clickable(view);
		return false;
	}));
	if (!result) {
		list->enumerateForumThreadBars(Floating([&](
				not_null<Element*> view,
				int itemtop,
				int top) {
			if (top + height <= point.y()) {
				return false;
			}
			const auto bar = view->Get<HistoryView::ForumThreadBar>();
			result = result || over(
				view,
				top,
				bar ? bar->width : list->_forumThreadBarWidth);
			return true;
		}));
	}
	return result;
}

std::unique_ptr<WidgetTouch> ListTouch::Create(not_null<HistoryInner*> list) {
	return std::make_unique<WidgetTouch>(WidgetTouchArgs{
		.list = list,
		.viewAt = [=](QPoint point) -> Element* {
			list->adjustCurrent(point.y());
			const auto history = list->_curHistory;
			if (!history) {
				return nullptr;
			}
			const auto &block = history->blocks[list->_curBlock];
			return block->messages[list->_curItem].get();
		},
		.mapToItem = [=](QPoint point, const Element *view) {
			return list->mapPointToItem(point, view);
		},
		.covered = [=](QPoint point) {
			return Covered(
				list,
				point,
				[=](not_null<Element*> view) { return list->_contentWidth; },
				[](not_null<Element*> view) { return true; });
		},
		.menu = [=](not_null<QContextMenuEvent*> e) {
			list->showContextMenu(e, true);
		},
		.resetSpeed = [=] {
			list->touchResetSpeed();
		},
		.dropTouch = [=] {
			list->_touchInProgress = false;
		},
	});
}

std::unique_ptr<WidgetTouch> ListTouch::Create(
		not_null<HistoryView::ListWidget*> list) {
	return std::make_unique<WidgetTouch>(WidgetTouchArgs{
		.list = list,
		.viewAt = [=](QPoint point) {
			return list->lookupItemByY(point.y());
		},
		.mapToItem = [=](QPoint point, const Element *view) {
			return list->mapPointToItem(point, view);
		},
		.covered = [=](QPoint point) {
			return Covered(
				list,
				point,
				[](not_null<Element*> view) { return view->width(); },
				[=](not_null<Element*> view) {
					auto link = ClickHandlerPtr();
					list->_delegate->listUpdateDateLink(link, view);
					return (link != nullptr);
				});
		},
		.menu = [=](not_null<QContextMenuEvent*> e) {
			list->showContextMenu(e, true);
		},
		.resetSpeed = [=] {
			list->touchResetSpeed();
		},
		.dropTouch = [=] {
			list->_touchInProgress = false;
		},
	});
}

} // namespace Tgw

namespace HistoryView {

Ui::Controls::SwipeHandoff ListWidget::touchSwipeHandoff() {
	return _widgetTouch->swipeHandoff();
}

} // namespace HistoryView
