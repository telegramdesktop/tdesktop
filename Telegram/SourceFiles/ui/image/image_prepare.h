/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

enum class ImageRoundRadius {
	None,
	Large,
	Small,
	Ellipse,
};

namespace Images {

QPixmap PixmapFast(QImage &&image);

QImage prepareBlur(QImage image);
void prepareRound(
	QImage &image,
	ImageRoundRadius radius,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareRound(
	QImage &image,
	QImage *cornerMasks,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareCircle(QImage &image);
QImage prepareColored(style::color add, QImage image);
QImage prepareOpaque(QImage image);

enum class Option {
	None                  = 0,
	Smooth                = (1 << 0),
	Blurred               = (1 << 1),
	Circled               = (1 << 2),
	RoundedLarge          = (1 << 3),
	RoundedSmall          = (1 << 4),
	RoundedTopLeft        = (1 << 5),
	RoundedTopRight       = (1 << 6),
	RoundedBottomLeft     = (1 << 7),
	RoundedBottomRight    = (1 << 8),
	RoundedAll            = (None
		| RoundedTopLeft
		| RoundedTopRight
		| RoundedBottomLeft
		| RoundedBottomRight),
	Colored               = (1 << 9),
	TransparentBackground = (1 << 10),
};
using Options = base::flags<Option>;
inline constexpr auto is_flag_type(Option) { return true; };

QImage prepare(QImage img, int w, int h, Options options, int outerw, int outerh, const style::color *colored = nullptr);

} // namespace Images
