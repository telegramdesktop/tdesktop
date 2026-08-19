/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "base/flat_map.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ThanosEffect;
class ScrollArea;
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {
class Element;
} // namespace HistoryView

class HistoryItem;

namespace Ui {

struct CollapseGap {
	int absY = -1;
	int height = 0;
	int dateHeight = 0;

	friend bool operator==(const CollapseGap&, const CollapseGap&) = default;
};

[[nodiscard]] int CollapseDateShift(
	const std::vector<CollapseGap> &gaps,
	int itemTop);

class ThanosEffectController final {
public:
	struct Delegate {
		Fn<HistoryView::Element*(not_null<const HistoryItem*>)> viewForItem;
		Fn<int(not_null<const HistoryView::Element*>)> itemTop;
		Fn<int()> visibleAreaTop;
		Fn<int()> visibleAreaBottom;
		Fn<int()> contentWidth;
		Fn<ChatPaintContext(QRect)> preparePaintContext;
		Fn<QWidget*()> window;
		Fn<int()> scrollTop;
		Fn<int()> scrollTopMax;
		Fn<not_null<QWidget*>()> scrollWidget;
		Fn<void(int scrollTop)> scrollToY;
		Fn<void()> collapseGapsUpdated;
	};

	ThanosEffectController(
		not_null<Main::Session*> session,
		Delegate delegate,
		rpl::lifetime &lifetime);
	~ThanosEffectController();

	void captureOnRemoval(not_null<const HistoryItem*> item);
	void clearPreCaptured();
	void pinScroll();
	void shiftGaps(int delta);
	void notePrependBaseline(int contentHeight);
	void applyPrependBaseline(int contentHeight);

	[[nodiscard]] const std::vector<CollapseGap> &renderGaps() const {
		return _renderGaps;
	}
	[[nodiscard]] int removalHeight() const {
		return _removalHeight;
	}
	void clearRemovalHeight() {
		_removalHeight = 0;
	}

private:
	struct PreCapturedView {
		int height = 0;
		int top = 0;
		int dateHeight = 0;
	};

	struct CollapseGapState {
		int absY = -1;
		int startHeight = 0;
		int currentHeight = 0;
		int originalHeight = 0;
		int dateHeight = 0;
	};

	void captureItemsBatch(
		const std::vector<not_null<HistoryItem*>> &items);
	[[nodiscard]] bool captureView(
		not_null<const HistoryView::Element*> view,
		int viewHeight,
		int viewTop);
	void startCollapseAnimation(int height, int itemTop, int dateHeight);
	void collapseAnimationCallback();
	void syncCollapseGapsToHost();
	void ensureScrollBaseline();

	const not_null<Main::Session*> _session;
	const Delegate _delegate;

	std::unique_ptr<ThanosEffect> _thanosEffect;
	base::flat_map<FullMsgId, PreCapturedView> _preCaptured;
	std::vector<CollapseGap> _renderGaps;

	std::vector<CollapseGapState> _collapseGaps;
	Animations::Simple _collapseAnimation;

	bool _inPinScroll = false;
	int _gapsShift = 0;
	int _prependBaseline = 0;
	bool _prependPending = false;
	int _removalHeight = 0;

	int _savedScrollTop = 0;
	bool _restoreScrollPending = false;
	bool _wasAtBottom = false;
};

} // namespace Ui
