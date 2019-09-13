/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/style/style_core.h"

#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/palette.h"

#include <QtGui/QPainter>

namespace style {
namespace internal {
namespace {

constexpr auto kMinContrastAlpha = 64;
constexpr auto kMinContrastDistance = 64 * 64 * 4;
constexpr auto kContrastDeltaL = 64;

int DevicePixelRatioValue = 1;
bool RightToLeftValue = false;

std::vector<internal::ModuleBase*> &StyleModules() {
	static auto result = std::vector<internal::ModuleBase*>();
	return result;
}

void startModules(int scale) {
	for (const auto module : StyleModules()) {
		module->start(scale);
	}
}

} // namespace

void registerModule(ModuleBase *module) {
	StyleModules().push_back(module);
}

} // namespace internal

bool RightToLeft() {
	return internal::RightToLeftValue;
}

void SetRightToLeft(bool rtl) {
	internal::RightToLeftValue = rtl;
}

void startManager(int scale) {
	internal::registerFontFamily("Open Sans");
	internal::startModules(scale);
}

void stopManager() {
	internal::destroyFonts();
	internal::destroyIcons();
}

void colorizeImage(const QImage &src, QColor c, QImage *outResult, QRect srcRect, QPoint dstPoint) {
	// In background_box ColorizePattern we use the fact that
	// colorizeImage takes only first byte of the mask, so it
	// could be used for wallpaper patterns, which have values
	// in ranges (0, 0, 0, 0) to (0, 0, 0, 255) (only 'alpha').
	if (srcRect.isNull()) {
		srcRect = src.rect();
	} else {
		Assert(src.rect().contains(srcRect));
	}
	auto width = srcRect.width();
	auto height = srcRect.height();
	Assert(outResult && outResult->rect().contains(QRect(dstPoint, srcRect.size())));

	auto pattern = anim::shifted(c);

	auto resultBytesPerPixel = (src.depth() >> 3);
	constexpr auto resultIntsPerPixel = 1;
	auto resultIntsPerLine = (outResult->bytesPerLine() >> 2);
	auto resultIntsAdded = resultIntsPerLine - width * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(outResult->bits()) + dstPoint.y() * resultIntsPerLine + dstPoint.x() * resultIntsPerPixel;
	Assert(resultIntsAdded >= 0);
	Assert(outResult->depth() == static_cast<int>((resultIntsPerPixel * sizeof(uint32)) << 3));
	Assert(outResult->bytesPerLine() == (resultIntsPerLine << 2));

	auto maskBytesPerPixel = (src.depth() >> 3);
	auto maskBytesPerLine = src.bytesPerLine();
	auto maskBytesAdded = maskBytesPerLine - width * maskBytesPerPixel;
	auto maskBytes = src.constBits() + srcRect.y() * maskBytesPerLine + srcRect.x() * maskBytesPerPixel;
	Assert(maskBytesAdded >= 0);
	Assert(src.depth() == (maskBytesPerPixel << 3));
	for (int y = 0; y != height; ++y) {
		for (int x = 0; x != width; ++x) {
			auto maskOpacity = static_cast<anim::ShiftedMultiplier>(*maskBytes) + 1;
			*resultInts = anim::unshifted(pattern * maskOpacity);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}

	outResult->setDevicePixelRatio(src.devicePixelRatio());
}

QBrush transparentPlaceholderBrush() {
	auto size = st::transparentPlaceholderSize * DevicePixelRatio();
	auto transparent = QImage(2 * size, 2 * size, QImage::Format_ARGB32_Premultiplied);
	transparent.fill(st::mediaviewTransparentBg->c);
	{
		QPainter p(&transparent);
		p.fillRect(0, size, size, size, st::mediaviewTransparentFg);
		p.fillRect(size, 0, size, size, st::mediaviewTransparentFg);
	}
	transparent.setDevicePixelRatio(DevicePixelRatio());
	return QBrush(transparent);

}

namespace internal {

QImage createCircleMask(int size, QColor bg, QColor fg) {
	int realSize = size * DevicePixelRatio();
#ifndef OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_Grayscale8);
#else // OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_RGB32);
#endif // OS_MAC_OLD
	{
		QPainter p(&result);
		PainterHighQualityEnabler hq(p);

		p.fillRect(0, 0, realSize, realSize, bg);
		p.setPen(Qt::NoPen);
		p.setBrush(fg);
		p.drawEllipse(0, 0, realSize, realSize);
	}
	result.setDevicePixelRatio(DevicePixelRatio());
	return result;
}

[[nodiscard]] bool GoodForContrast(const QColor &c1, const QColor &c2) {
	auto r1 = 0;
	auto g1 = 0;
	auto b1 = 0;
	auto r2 = 0;
	auto g2 = 0;
	auto b2 = 0;
	c1.getRgb(&r1, &g1, &b1);
	c2.getRgb(&r2, &g2, &b2);
	const auto rMean = (r1 + r2) / 2;
	const auto r = r1 - r2;
	const auto g = g1 - g2;
	const auto b = b1 - b2;
	const auto distance = (((512 + rMean) * r * r) >> 8)
		+ (4 * g * g)
		+ (((767 - rMean) * b * b) >> 8);
	return (distance > kMinContrastDistance);
}

QColor EnsureContrast(const QColor &over, const QColor &under) {
	auto overH = 0;
	auto overS = 0;
	auto overL = 0;
	auto overA = 0;
	auto underH = 0;
	auto underS = 0;
	auto underL = 0;
	over.getHsl(&overH, &overS, &overL, &overA);
	under.getHsl(&underH, &underS, &underL);
	const auto good = GoodForContrast(over, under);
	if (overA >= kMinContrastAlpha && good) {
		return over;
	}
	const auto newA = std::max(overA, kMinContrastAlpha);
	const auto newL = (overL > underL && overL + kContrastDeltaL <= 255)
		? (overL + kContrastDeltaL)
		: (overL < underL && overL - kContrastDeltaL >= 0)
		? (overL - kContrastDeltaL)
		: (underL > 128)
		? (underL - kContrastDeltaL)
		: (underL + kContrastDeltaL);
	return QColor::fromHsl(overH, overS, newL, newA).toRgb();
}

void EnsureContrast(ColorData &over, const ColorData &under) {
	const auto good = EnsureContrast(over.c, under.c);
	if (over.c != good) {
		over.c = good;
		over.p = QPen(good);
		over.b = QBrush(good);
	}
}

} // namespace internal
} // namespace style
