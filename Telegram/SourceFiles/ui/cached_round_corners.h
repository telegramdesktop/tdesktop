/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

class Painter;

enum class ImageRoundRadius;

namespace Ui {

enum CachedRoundCorners : int {
	SmallMaskCorners = 0x00, // for images
	LargeMaskCorners,

	BoxCorners,
	MenuCorners,
	BotKbOverCorners,
	StickerCorners,
	StickerSelectedCorners,
	SelectedOverlaySmallCorners,
	SelectedOverlayLargeCorners,
	DateCorners,
	DateSelectedCorners,
	OverviewVideoCorners,
	OverviewVideoSelectedCorners,
	ForwardCorners,
	MediaviewSaveCorners,
	EmojiHoverCorners,
	StickerHoverCorners,
	BotKeyboardCorners,
	PhotoSelectOverlayCorners,

	Doc1Corners,
	Doc2Corners,
	Doc3Corners,
	Doc4Corners,

	InShadowCorners, // for photos without bg
	InSelectedShadowCorners,

	MessageInCorners, // with shadow
	MessageInSelectedCorners,
	MessageOutCorners,
	MessageOutSelectedCorners,

	RoundCornersCount
};

void FillComplexOverlayRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners);
void FillComplexLocationRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners);

void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full);
inline void FillRoundRect(Painter &p, const QRect &rect, style::color bg, CachedRoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full) {
	return FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, index, shadow, parts);
}
void FillRoundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, CachedRoundCorners index, RectParts parts = RectPart::Full);
inline void FillRoundShadow(Painter &p, const QRect &rect, style::color shadow, CachedRoundCorners index, RectParts parts = RectPart::Full) {
	return FillRoundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), shadow, index, parts);
}
void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full);
inline void FillRoundRect(Painter &p, const QRect &rect, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full) {
	return FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, radius, parts);
}

void StartCachedCorners();
void FinishCachedCorners();

} // namespace Ui
