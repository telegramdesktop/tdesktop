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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/player_wrap_widget.h"

#include "ui/widgets/shadow.h"

namespace Window {

PlayerWrapWidget::PlayerWrapWidget(QWidget *parent)
: Parent(parent, object_ptr<Media::Player::Widget>(parent)) {
	sizeValue()
		| rpl::start_with_next([this](const QSize &size) {
			updateShadowGeometry(size);
		}, lifetime());
}

void PlayerWrapWidget::updateShadowGeometry(const QSize &size) {
	auto skip = Adaptive::OneColumn() ? 0 : st::lineWidth;
	entity()->setShadowGeometryToLeft(
		skip,
		size.height() - st::lineWidth,
		size.width() - skip,
		st::lineWidth);
}

} // namespace Window
