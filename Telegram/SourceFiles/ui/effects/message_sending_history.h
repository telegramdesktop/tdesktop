/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

class HistoryItem;

namespace HistoryView {
enum class ElementChatMode : char;
} // namespace HistoryView

namespace Ui {

struct ItemRevealAnimation {
	Animations::Simple animation;
	Fn<int(int height, int lift, float64 progress)> compute;
	int height = 0;
	int lift = 0;

	[[nodiscard]] int current() const;
};

struct ItemRevealLift {
	const HistoryItem *item = nullptr;
	int lift = 0;

	[[nodiscard]] int revealHeight() const;
};

[[nodiscard]] bool TooWideForTextSending(
	HistoryView::ElementChatMode mode,
	int width,
	not_null<HistoryItem*> item);

[[nodiscard]] int StartItemReveal(
	ItemRevealAnimation &reveal,
	int height,
	Fn<int(int height, int lift, float64 progress)> compute,
	ItemRevealLift *pending = nullptr,
	const HistoryItem *item = nullptr);
[[nodiscard]] bool LiftItemReveal(
	ItemRevealAnimation *reveal,
	ItemRevealLift *pending,
	not_null<const HistoryItem*> item,
	int lift);
[[nodiscard]] int ReleaseItemRevealLift(ItemRevealLift &pending);

} // namespace Ui
