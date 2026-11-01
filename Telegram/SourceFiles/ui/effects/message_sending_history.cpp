/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/message_sending_history.h"

#include "history/view/history_view_element.h"
#include "history/history_item.h"

#include "styles/style_window.h"

namespace Ui {
namespace {

[[nodiscard]] int RevealHeightAt(
		const ItemRevealAnimation &reveal,
		float64 progress) {
	return reveal.compute
		? reveal.compute(reveal.height, reveal.lift, progress)
		: anim::interpolate(reveal.height, 0, progress);
}

} // namespace

int ItemRevealAnimation::current() const {
	return RevealHeightAt(*this, animation.value(1.));
}

int ItemRevealLift::revealHeight() const {
	return -lift;
}

bool TooWideForTextSending(
		HistoryView::ElementChatMode mode,
		int width,
		not_null<HistoryItem*> item) {
	return (mode == HistoryView::ElementChatMode::Default)
		&& (width > st::columnMaximalWidthLeft)
		&& !item->media();
}

int StartItemReveal(
		ItemRevealAnimation &reveal,
		int height,
		Fn<int(int height, int lift, float64 progress)> compute,
		ItemRevealLift *pending,
		const HistoryItem *item) {
	reveal.compute = std::move(compute);
	reveal.height = height;
	reveal.lift = 0;
	if (!pending || !item || pending->item != item) {
		return RevealHeightAt(reveal, 0.);
	}
	const auto held = pending->revealHeight();
	if (reveal.compute) {
		reveal.lift = pending->lift;
	}
	*pending = {};
	return RevealHeightAt(reveal, 0.) - held;
}

bool LiftItemReveal(
		ItemRevealAnimation *reveal,
		ItemRevealLift *pending,
		not_null<const HistoryItem*> item,
		int lift) {
	if (lift <= 0) {
		return false;
	} else if (reveal && reveal->compute) {
		reveal->lift += lift;
		return true;
	} else if (!reveal && pending) {
		*pending = { .item = item.get(), .lift = lift };
		return true;
	}
	return false;
}

int ReleaseItemRevealLift(ItemRevealLift &pending) {
	return base::take(pending).lift;
}

} // namespace Ui
