/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/scroll_area.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Info {

extern const char kClassicProfileScroll[];

[[nodiscard]] bool UseClassicProfileScroll();

void SetupFlexibleRegularScroll(
	not_null<Ui::ScrollArea*> scroll,
	not_null<Ui::RpWidget*> inner,
	not_null<Ui::RpWidget*> pinnedToTop,
	Fn<void(int)> setScrollTopSkip,
	Fn<void(int)> setInnerTopReserve,
	Fn<void(QMargins)> setPaintPadding,
	Fn<void(rpl::producer<not_null<QEvent*>>)> setViewport,
	bool abortSnapOnExternalScroll = false);

struct FlexibleScrollData {
	rpl::event_stream<int> contentHeightValue;
	rpl::event_stream<int> fillerWidthValue;
	rpl::event_stream<> backButtonEnables;
};

class FlexibleScrollHelper final {
public:
	FlexibleScrollHelper(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::RpWidget*> inner,
		not_null<Ui::RpWidget*> pinnedToTop,
		Fn<void(QMargins)> setPaintPadding,
		Fn<void(rpl::producer<not_null<QEvent*>>&&)> setViewport,
		FlexibleScrollData &data,
		bool abortSnapOnExternalScroll = false);

private:
	void setupScrollAnimation();
	void setupScrollHandling();
	void scrollToY(int value);
	void applyScrollToPinnedLayout(int scrollCurrent);

	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<Ui::RpWidget*> _inner;
	const not_null<Ui::RpWidget*> _pinnedToTop;
	const Fn<void(QMargins)> _setPaintPadding;
	const Fn<void(rpl::producer<not_null<QEvent*>>&&)> _setViewport;
	FlexibleScrollData &_data;
	const bool _abortSnapOnExternalScroll = false;

	Ui::Animations::Basic _scrollAnimation;
	int _scrollTopFrom = 0;
	int _scrollTopTo = 0;
	crl::time _timeOffset = 0;
	int _lastScrollApplied = 0;
	int _lastScrollSeen = -1;
	rpl::lifetime _filterLifetime;
};

} // namespace Info
