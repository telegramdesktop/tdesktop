/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

enum class RectPart {
	None = 0,

	TopLeft = (1 << 0),
	Top = (1 << 1),
	TopRight = (1 << 2),
	Left = (1 << 3),
	Center = (1 << 4),
	Right = (1 << 5),
	BottomLeft = (1 << 6),
	Bottom = (1 << 7),
	BottomRight = (1 << 8),

	FullTop = TopLeft | Top | TopRight,
	NoTopBottom = Left | Center | Right,
	FullBottom = BottomLeft | Bottom | BottomRight,
	NoTop = NoTopBottom | FullBottom,
	NoBottom = FullTop | NoTopBottom,

	FullLeft = TopLeft | Left | BottomLeft,
	NoLeftRight = Top | Center | Bottom,
	FullRight = TopRight | Right | BottomRight,
	NoLeft = NoLeftRight | FullRight,
	NoRight = FullLeft | NoLeftRight,

	AllCorners = TopLeft | TopRight | BottomLeft | BottomRight,
	AllSides = Top | Bottom | Left | Right,

	Full = FullTop | NoTop,
};
using RectParts = base::flags<RectPart>;
inline constexpr auto is_flag_type(RectPart) { return true; };

inline bool IsTopCorner(RectPart corner) {
	return (corner == RectPart::TopLeft) || (corner == RectPart::TopRight);
}

inline bool IsBottomCorner(RectPart corner) {
	return (corner == RectPart::BottomLeft) || (corner == RectPart::BottomRight);
}

inline bool IsLeftCorner(RectPart corner) {
	return (corner == RectPart::TopLeft) || (corner == RectPart::BottomLeft);
}

inline bool IsRightCorner(RectPart corner) {
	return (corner == RectPart::TopRight) || (corner == RectPart::BottomRight);
}
