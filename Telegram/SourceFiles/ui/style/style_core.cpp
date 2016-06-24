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
	styleModules.makeIfNull();
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
	internal::loadSprite();
	internal::startModules();
}

void stopManager() {
	internal::stopModules();
	internal::destroyFonts();
	internal::destroyColors();
}

QImage colorizeImage(const QImage &src, const color &c, const QRect &r) {
	t_assert(r.x() >= 0 && src.width() >= r.x() + r.width());
	t_assert(r.y() >= 0 && src.height() >= r.y() + r.height());

	int a = c->c.alpha() + 1;
	int fg_r = c->c.red() * a, fg_g = c->c.green() * a, fg_b = c->c.blue() * a, fg_a = 255 * a;

	QImage result(r.width(), r.height(), QImage::Format_ARGB32_Premultiplied);
	auto bits = result.bits();
	auto maskbits = src.constBits();
	int bpp = result.depth(), maskbpp = src.depth();
	int bpl = result.bytesPerLine(), maskbpl = src.bytesPerLine();
	for (int x = 0, xoffset = r.x(); x < r.width(); ++x) {
		for (int y = 0, yoffset = r.y(); y < r.height(); ++y) {
			int s = y * bpl + ((x * bpp) >> 3);
			int o = maskbits[(y + yoffset) * maskbpl + (((x + xoffset) * maskbpp) >> 3)] + 1;
			bits[s + 0] = (fg_b * o) >> 16;
			bits[s + 1] = (fg_g * o) >> 16;
			bits[s + 2] = (fg_r * o) >> 16;
			bits[s + 3] = (fg_a * o) >> 16;
		}
	}
	return result;
}

namespace internal {

QImage createCircleMask(int size, const QColor &bg, const QColor &fg) {
	int realSize = size * cIntRetinaFactor();
	auto result = QImage(realSize, realSize, QImage::Format::Format_Grayscale8);
	{
		QPainter pcircle(&result);
		pcircle.setRenderHint(QPainter::HighQualityAntialiasing, true);
		pcircle.fillRect(0, 0, realSize, realSize, bg);
		pcircle.setPen(Qt::NoPen);
		pcircle.setBrush(fg);
		// pcircle.drawRoundedRect(0, 0, realSize, realSize);
		QRectF rectangle(0, 0, realSize, realSize);
		pcircle.drawRoundedRect(rectangle, 2.0, 2.0);
	}
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namespace internal
} // namespace style
