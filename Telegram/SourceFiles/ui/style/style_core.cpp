/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/style/style_core.h"

namespace style {
namespace internal {
namespace {

using ModulesList = QList<internal::ModuleBase*>;
NeverFreedPointer<ModulesList> styleModules;

void startModules() {
	if (!styleModules) return;

	for_const (auto module, *styleModules) {
		module->start();
	}
}

void stopModules() {
	if (!styleModules) return;

	for_const (auto module, *styleModules) {
		module->stop();
	}
}

} // namespace

void registerModule(ModuleBase *module) {
	styleModules.createIfNull();
	styleModules->push_back(module);
}

void unregisterModule(ModuleBase *module) {
	styleModules->removeOne(module);
	if (styleModules->isEmpty()) {
		styleModules.clear();
	}
}

} // namespace internal

void startManager() {
	if (cIntRetinaFactor() * cConfigScale() > kInterfaceScaleMax) {
		cSetConfigScale(kInterfaceScaleDefault);
	}

	internal::registerFontFamily(qsl("Open Sans"));
	internal::startModules();
}

void stopManager() {
	internal::stopModules();
	internal::destroyFonts();
	internal::destroyIcons();
}

void colorizeImage(const QImage &src, QColor c, QImage *outResult, QRect srcRect, QPoint dstPoint) {
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
	auto size = st::transparentPlaceholderSize * cIntRetinaFactor();
	auto transparent = QImage(2 * size, 2 * size, QImage::Format_ARGB32_Premultiplied);
	transparent.fill(st::mediaviewTransparentBg->c);
	{
		Painter p(&transparent);
		p.fillRect(rtlrect(0, size, size, size, 2 * size), st::mediaviewTransparentFg);
		p.fillRect(rtlrect(size, 0, size, size, 2 * size), st::mediaviewTransparentFg);
	}
	transparent.setDevicePixelRatio(cRetinaFactor());
	return QBrush(transparent);

}

namespace internal {

QImage createCircleMask(int size, QColor bg, QColor fg) {
	int realSize = size * cIntRetinaFactor();
#ifndef OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_Grayscale8);
#else // OS_MAC_OLD
	auto result = QImage(realSize, realSize, QImage::Format::Format_RGB32);
#endif // OS_MAC_OLD
	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.fillRect(0, 0, realSize, realSize, bg);
		p.setPen(Qt::NoPen);
		p.setBrush(fg);
		p.drawEllipse(0, 0, realSize, realSize);
	}
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namespace internal
} // namespace style
