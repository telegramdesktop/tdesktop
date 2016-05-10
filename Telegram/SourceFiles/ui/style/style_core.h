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
#pragma once

#include "ui/style/style_core_types.h"

inline QPoint rtlpoint(int x, int y, int outerw) {
	return QPoint(rtl() ? (outerw - x) : x, y);
}
inline QPoint rtlpoint(const QPoint &p, int outerw) {
	return rtl() ? QPoint(outerw - p.x(), p.y()) : p;
}
inline QRect rtlrect(int x, int y, int w, int h, int outerw) {
	return QRect(rtl() ? (outerw - x - w) : x, y, w, h);
}
inline QRect rtlrect(const QRect &r, int outerw) {
	return rtl() ? QRect(outerw - r.x() - r.width(), r.y(), r.width(), r.height()) : r;
}
inline QRect centerrect(const QRect &inRect, const QRect &rect) {
	return QRect(inRect.x() + (inRect.width() - rect.width()) / 2, inRect.y() + (inRect.height() - rect.height()) / 2, rect.width(), rect.height());
}

namespace style {
namespace internal {

// Objects of derived classes are created in global scope.
// They call [un]registerModule() in [de|con]structor.
class ModuleBase {
public:
	virtual void start() = 0;
	virtual void stop() = 0;
};

void registerModule(ModuleBase *module);
void unregisterModule(ModuleBase *module);

} // namespace internal

void startManager();
void stopManager();

QImage colorizeImage(const QImage &src, const color &c, const QRect &r);

} // namespace style

inline QRect centersprite(const QRect &inRect, const style::sprite &sprite) {
	return centerrect(inRect, QRect(QPoint(0, 0), sprite.pxSize()));
}
