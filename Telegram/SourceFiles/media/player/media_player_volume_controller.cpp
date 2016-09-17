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
#include "media/player/media_player_volume_controller.h"

#include "styles/style_media_player.h"

namespace Media {
namespace Player {

VolumeController::VolumeController(QWidget *parent) : TWidget(parent) {
	resize(st::mediaPlayerVolumeWidth, 2 * st::mediaPlayerPlaybackPadding + st::mediaPlayerPlaybackLine);
}

void VolumeController::paintEvent(QPaintEvent *e) {
	Painter p(this);

	st::mediaPlayerVolumeIcon0.paint(p, QPoint(0, (height() - st::mediaPlayerVolumeIcon0.height()) / 2), width());

	auto left = rtl() ? 0 : width() - st::mediaPlayerVolumeLength;
	p.fillRect(left, st::mediaPlayerPlaybackPadding, st::mediaPlayerVolumeLength, st::mediaPlayerPlaybackLine, st::mediaPlayerPlaybackBg);
}

void VolumeController::mousePressEvent(QMouseEvent *e) {
}

void VolumeController::mouseMoveEvent(QMouseEvent *e) {
}

void VolumeController::mouseReleaseEvent(QMouseEvent *e) {
}

} // namespace Player
} // namespace Media
