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
#include "ui/style/style_core_types.h"

namespace style {
namespace internal {
namespace {

int spriteWidthValue = 0;
QPixmap *spriteData = nullptr;

} // namespace

void loadSprite() {
	QString spriteFilePostfix;
	if (cRetina() || cScale() == dbisTwo) {
		spriteFilePostfix = qsl("_200x");
	} else if (cScale() == dbisOneAndQuarter) {
		spriteFilePostfix = qsl("_125x");
	} else if (cScale() == dbisOneAndHalf) {
		spriteFilePostfix = qsl("_150x");
	}
	QString spriteFile = qsl(":/gui/art/sprite") + spriteFilePostfix + qsl(".png");
	if (rtl()) {
		spriteData = new QPixmap(QPixmap::fromImage(QImage(spriteFile).mirrored(true, false)));
	} else {
		spriteData = new QPixmap(spriteFile);
	}
	if (cRetina()) spriteData->setDevicePixelRatio(cRetinaFactor());
	spriteWidthValue = spriteData->width();
}

int spriteWidth() {
	return spriteWidthValue;
}

} // namespace internal

const QPixmap &spritePixmap() {
	return *internal::spriteData;
}

} // namespace style
