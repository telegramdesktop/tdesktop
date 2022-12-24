/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

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

void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, CachedRoundCorners index);
inline void FillRoundRect(QPainter &p, const QRect &rect, style::color bg, CachedRoundCorners index) {
	FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, index);
}

[[nodiscard]] const CornersPixmaps &CachedCornerPixmaps(CachedRoundCorners index);
[[nodiscard]] CornersPixmaps PrepareCornerPixmaps(
	int32 radius,
	style::color bg,
	const style::color *sh = nullptr);
[[nodiscard]] CornersPixmaps PrepareCornerPixmaps(
	ImageRoundRadius radius,
	style::color bg,
	const style::color *sh = nullptr);
void FillRoundRect(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, const CornersPixmaps &corners);
inline void FillRoundRect(QPainter &p, const QRect &rect, style::color bg, const CornersPixmaps &corners) {
	return FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, corners);
}
void FillRoundShadow(QPainter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, const CornersPixmaps &corners);
inline void FillRoundShadow(QPainter &p, const QRect &rect, style::color shadow, const CornersPixmaps &corners) {
	FillRoundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), shadow, corners);
}

enum class CachedCornerRadius {
	Small,
	ThumbSmall,
	ThumbLarge,
	BubbleSmall,
	BubbleLarge,

	kCount,
};
[[nodiscard]] int CachedCornerRadiusValue(CachedCornerRadius tag);

[[nodiscard]] const std::array<QImage, 4> &CachedCornersMasks(
	CachedCornerRadius radius);

void StartCachedCorners();
void FinishCachedCorners();

} // namespace Ui
