/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_scale.h"
#include "ui/style/style_core_types.h"
#include "ui/style/style_core_direction.h"

#include <rpl/producer.h>

namespace style {
namespace internal {

// Objects of derived classes are created in global scope.
// They call [un]registerModule() in [de|con]structor.
class ModuleBase {
public:
	virtual void start(int scale) = 0;

	virtual ~ModuleBase() = default;

};

void registerModule(ModuleBase *module);

[[nodiscard]] QColor EnsureContrast(const QColor &over, const QColor &under);
void EnsureContrast(ColorData &over, const ColorData &under);

void StartShortAnimation();
void StopShortAnimation();

} // namespace internal

void startManager(int scale);
void stopManager();

[[nodiscard]] rpl::producer<> PaletteChanged();
void NotifyPaletteChanged();

[[nodiscard]] rpl::producer<bool> ShortAnimationPlaying();

const style::font &MonospaceFont();

// *outResult must be r.width() x r.height(), ARGB32_Premultiplied.
// QRect(0, 0, src.width(), src.height()) must contain r.
void colorizeImage(const QImage &src, QColor c, QImage *outResult, QRect srcRect = QRect(), QPoint dstPoint = QPoint(0, 0));

inline QImage colorizeImage(const QImage &src, QColor c, QRect srcRect = QRect()) {
	if (srcRect.isNull()) srcRect = src.rect();
	auto result = QImage(srcRect.size(), QImage::Format_ARGB32_Premultiplied);
	colorizeImage(src, c, &result, srcRect);
	return result;
}

inline QImage colorizeImage(const QImage &src, const color &c, QRect srcRect = QRect()) {
	return colorizeImage(src, c->c, srcRect);
}

QBrush transparentPlaceholderBrush();

namespace internal {

QImage createCircleMask(int size, QColor bg, QColor fg);

} // namespace internal

inline QImage createCircleMask(int size) {
	return internal::createCircleMask(size, QColor(0, 0, 0), QColor(255, 255, 255));
}

inline QImage createInvertedCircleMask(int size) {
	return internal::createCircleMask(size, QColor(255, 255, 255), QColor(0, 0, 0));
}

} // namespace style
