/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "emoji.h"

class QPainter;

namespace Tgw {

struct EmojiSides {
	int normal = 0;
	int large = 0;
	int isolated = 0;
};

[[nodiscard]] EmojiSides CurrentEmojiSides();
[[nodiscard]] rpl::producer<> EmojiRefreshes();
void FinishEmojiImages();

void ConfigureEmojiTiles(EmojiSides sides, Fn<void()> landed);
void PaintEmoji(
	QPainter &p,
	EmojiPtr emoji,
	const QRectF &box,
	float64 deviceSide);
[[nodiscard]] uint64 EmojiTileWaits();
void ClearEmojiTiles();

} // namespace Tgw
