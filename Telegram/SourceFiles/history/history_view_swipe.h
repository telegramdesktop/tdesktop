/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class ElasticScroll;
class RpWidget;
class ScrollArea;
} // namespace Ui

namespace HistoryView {

struct ChatPaintGestureHorizontalData;
struct SwipeBackResult;

constexpr auto kMsgBareIdSwipeBack = std::numeric_limits<int64>::max() - 77;

struct SwipeHandlerFinishData {
	Fn<void(void)> callback;
	int64 msgBareId = 0;
};

using Scroll = std::variant<
	v::null_t,
	not_null<Ui::ScrollArea*>,
	not_null<Ui::ElasticScroll*>>;

void SetupSwipeHandler(
	not_null<Ui::RpWidget*> widget,
	Scroll scroll,
	Fn<void(ChatPaintGestureHorizontalData)> update,
	Fn<SwipeHandlerFinishData(int, Qt::LayoutDirection)> generateFinishByTop,
	rpl::producer<bool> dontStart = nullptr);

SwipeBackResult SetupSwipeBack(
	not_null<Ui::RpWidget*> widget,
	Fn<std::pair<QColor, QColor>()> colors);

} // namespace HistoryView
