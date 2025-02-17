/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
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

void SetupSwipeHandler(
	not_null<Ui::RpWidget*> widget,
	not_null<Ui::ScrollArea*> scroll,
	Fn<void(ChatPaintGestureHorizontalData)> update,
	Fn<SwipeHandlerFinishData(int, Qt::LayoutDirection)> generateFinishByTop,
	rpl::producer<bool> dontStart = nullptr);

SwipeBackResult SetupSwipeBack(
	not_null<Ui::RpWidget*> widget,
	Fn<std::pair<QColor, QColor>()> colors);

} // namespace HistoryView
