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

struct CornersPixmaps {
	QPixmap p[4];
};

enum CachedRoundCorners : int {
	BoxCorners,
	MenuCorners,
	DateCorners,
	OverviewVideoCorners,
	OverviewVideoSelectedCorners,
	ForwardCorners,
	MediaviewSaveCorners,
	StickerHoverCorners,
	BotKeyboardCorners,

	Doc1Corners,
	Doc2Corners,
	Doc3Corners,
	Doc4Corners,

	RoundCornersCount
};

void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full);
inline void FillRoundRect(Painter &p, const QRect &rect, style::color bg, CachedRoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full) {
	FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, index, shadow, parts);
}
void FillRoundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, CachedRoundCorners index, RectParts parts = RectPart::Full);
inline void FillRoundShadow(Painter &p, const QRect &rect, style::color shadow, CachedRoundCorners index, RectParts parts = RectPart::Full) {
	FillRoundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), shadow, index, parts);
}
void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full);
inline void FillRoundRect(Painter &p, const QRect &rect, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full) {
	FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, radius, parts);
}

[[nodiscard]] CornersPixmaps PrepareCornerPixmaps(
	int32 radius,
	style::color bg,
	const style::color *sh);
[[nodiscard]] CornersPixmaps PrepareCornerPixmaps(
	ImageRoundRadius radius,
	style::color bg,
	const style::color *sh);
void FillRoundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corner, const style::color *shadow = nullptr, RectParts parts = RectPart::Full);
inline void FillRoundRect(Painter &p, const QRect &rect, style::color bg, const CornersPixmaps &corner, const style::color *shadow = nullptr, RectParts parts = RectPart::Full) {
	return FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, corner, shadow, parts);
}
void FillRoundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, const CornersPixmaps &corner, RectParts parts = RectPart::Full);
inline void FillRoundShadow(Painter &p, const QRect &rect, style::color shadow, const CornersPixmaps &corner, RectParts parts = RectPart::Full) {
	FillRoundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), shadow, corner, parts);
}

void StartCachedCorners();
void FinishCachedCorners();

} // namespace Ui
