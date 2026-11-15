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

void PaintEmoji(
	QPainter &p,
	EmojiPtr emoji,
	const QRectF &box,
	float64 deviceSide);
[[nodiscard]] rpl::producer<> EmojiRefreshes();

} // namespace Tgw
