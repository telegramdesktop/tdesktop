/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "emoji.h"

class QFontMetricsF;

namespace Tgw {

struct EmojiCell {
	int from = 0;
	int till = 0;
	EmojiPtr emoji = nullptr;
};

[[nodiscard]] std::vector<EmojiCell> FindEmojiCells(QStringView text);
[[nodiscard]] float64 EmojiAdvance(float64 fontSize);
[[nodiscard]] QRectF EmojiBox(float64 x, float64 fontSize);
float64 WalkTextWithEmoji(
	const QFontMetricsF &metrics,
	const QString &text,
	const std::vector<EmojiCell> &cells,
	float64 fontSize,
	Fn<void(float64, const QString &, EmojiPtr)> visit = nullptr);

} // namespace Tgw
