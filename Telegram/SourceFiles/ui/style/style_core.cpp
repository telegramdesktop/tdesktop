/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

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
	if (cRetina()) {
		cSetRealScale(dbisOne);
	}

	internal::registerFontFamily(qsl("Open Sans"));
	internal::startModules();
}

void stopManager() {
	internal::stopModules();
	internal::destroyFonts();
	internal::destroyColors();
	internal::destroyIcons();
}

QImage colorizeImage(const QImage &src, QColor color, const QRect &r) {
	t_assert(r.x() >= 0 && src.width() >= r.x() + r.width());
	t_assert(r.y() >= 0 && src.height() >= r.y() + r.height());

	auto initialAlpha = color.alpha() + 1;
	auto red = color.red() * initialAlpha;
	auto green = color.green() * initialAlpha;
	auto blue = color.blue() * initialAlpha;
	auto alpha = 255 * initialAlpha;
	auto alpha_red = static_cast<uint64>(alpha) | (static_cast<uint64>(red) << 32);
	auto green_blue = static_cast<uint64>(green) | (static_cast<uint64>(blue) << 32);

	auto result = QImage(r.width(), r.height(), QImage::Format_ARGB32_Premultiplied);
	auto resultBytesPerPixel = (src.depth() >> 3);
	auto resultIntsPerPixel = 1;
	auto resultIntsPerLine = (result.bytesPerLine() >> 2);
	auto resultIntsAdded = resultIntsPerLine - r.width() * resultIntsPerPixel;
	auto resultInts = reinterpret_cast<uint32*>(result.bits());
	t_assert(resultIntsAdded >= 0);
	t_assert(result.depth() == ((resultIntsPerPixel * sizeof(uint32)) << 3));
	t_assert(result.bytesPerLine() == (resultIntsPerLine << 2));

	auto maskBytesPerPixel = (src.depth() >> 3);
	auto maskBytesPerLine = src.bytesPerLine();
	auto maskBytesAdded = maskBytesPerLine - r.width() * maskBytesPerPixel;
	auto maskBytes = src.constBits() + r.y() * maskBytesPerLine + r.x() * maskBytesPerPixel;
	t_assert(maskBytesAdded >= 0);
	t_assert(src.depth() == (maskBytesPerPixel << 3));
	for (int y = 0; y != r.height(); ++y) {
		for (int x = 0; x != r.width(); ++x) {
			auto maskOpacity = static_cast<uint64>(*maskBytes) + 1;
			auto alpha_red_masked = (alpha_red * maskOpacity) >> 16;
			auto green_blue_masked = (green_blue * maskOpacity) >> 16;
			auto alpha = static_cast<uint32>(alpha_red_masked & 0xFF);
			auto red = static_cast<uint32>((alpha_red_masked >> 32) & 0xFF);
			auto green = static_cast<uint32>(green_blue_masked & 0xFF);
			auto blue = static_cast<uint32>((green_blue_masked >> 32) & 0xFF);
			*resultInts = blue | (green << 8) | (red << 16) | (alpha << 24);
			maskBytes += maskBytesPerPixel;
			resultInts += resultIntsPerPixel;
		}
		maskBytes += maskBytesAdded;
		resultInts += resultIntsAdded;
	}

	result.setDevicePixelRatio(src.devicePixelRatio());
	return std_::move(result);
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
		QPainter pcircle(&result);
		pcircle.setRenderHint(QPainter::HighQualityAntialiasing, true);
		pcircle.fillRect(0, 0, realSize, realSize, bg);
		pcircle.setPen(Qt::NoPen);
		pcircle.setBrush(fg);
		pcircle.drawEllipse(0, 0, realSize, realSize);
	}
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namespace internal
} // namespace style
