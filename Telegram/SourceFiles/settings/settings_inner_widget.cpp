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
#include "settings/settings_inner_widget.h"

namespace Settings {

InnerWidget::InnerWidget(QWidget *parent) : TWidget(parent)
{
}

void InnerWidget::resizeToWidth(int newWidth, int contentLeft) {
	int newHeight = resizeGetHeight(newWidth, contentLeft);
	resize(newWidth, newHeight);
}

int InnerWidget::resizeGetHeight(int newWidth, int contentLeft) {
	int result = 0;
	//if (_cover) {
	//	result += _cover->height();
	//}
	//for_const (auto blockData, _blocks) {
	//	if (blockData->isHidden()) {
	//		continue;
	//	}

	//	result += blockData->height();
	//}
	return result;
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	//for_const (auto blockData, _blocks) {
	//	int blockY = blockData->y();
	//	blockData->setVisibleTopBottom(visibleTop - blockY, visibleBottom - blockY);
	//}
}

} // namespace Settings
