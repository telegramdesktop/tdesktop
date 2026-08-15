/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QWheelEvent;

namespace Ui {
class ElasticScroll;
class RpWidget;
class ScrollArea;
} // namespace Ui

namespace Ui::Controls {

struct SwipeContextData;
struct SwipeBackResult;
struct SwipeHandlerInitData;

struct SwipeHandlerFinishData {
	Fn<void(void)> callback;
	int64 msgBareId = 0;
	float64 speedRatio = 1.0;
	crl::time reachRatioDuration = 0;
	bool keepRatioWithinRange = false;
	bool provideReachOutRatio = false;
};

using Scroll = std::variant<
	v::null_t,
	not_null<Ui::ScrollArea*>,
	not_null<Ui::ElasticScroll*>>;

struct SwipeHandlerArgs {
	not_null<Ui::RpWidget*> widget;
	Scroll scroll;
	Fn<void(SwipeContextData)> update;
	Fn<SwipeHandlerFinishData(SwipeHandlerInitData)> init;
	rpl::producer<bool> dontStart = nullptr;
	Fn<bool(not_null<QWheelEvent*>)> skipWheelEvent;
	rpl::lifetime *onLifetime = nullptr;
};

void SetupSwipeHandler(SwipeHandlerArgs &&args);

[[nodiscard]] SwipeBackResult SetupSwipeBack(
	not_null<Ui::RpWidget*> widget,
	Fn<std::pair<QColor, QColor>()> colors,
	bool mirrored = false,
	bool iconMirrored = false,
	Fn<int()> centerY = nullptr);

[[nodiscard]] SwipeHandlerFinishData DefaultSwipeBackHandlerFinishData(
	Fn<void(void)> callback);

} // namespace Ui::Controls
