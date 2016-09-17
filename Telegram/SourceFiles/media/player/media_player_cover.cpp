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
#include "media/player/media_player_cover.h"

#include "ui/flatlabel.h"
#include "ui/widgets/label_simple.h"
#include "ui/buttons/icon_button.h"
#include "media/player/media_player_playback.h"
#include "media/player/media_player_volume_controller.h"
#include "styles/style_media_player.h"

namespace Media {
namespace Player {

CoverWidget::CoverWidget(QWidget *parent) : TWidget(parent)
, _nameLabel(this)
, _timeLabel(this)
, _playback(this)
, _playPause(this, st::mediaPlayerPlayButton)
, _volumeController(this)
, _repeatTrack(this, st::mediaPlayerRepeatButton) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_playPause->setIcon(&st::mediaPlayerPauseIcon);
}

void CoverWidget::resizeEvent(QResizeEvent *e) {
	_nameLabel->moveToLeft(st::mediaPlayerPadding, st::mediaPlayerNameTop - st::mediaPlayerNameFont->ascent);
	_timeLabel->moveToRight(st::mediaPlayerPadding, st::mediaPlayerNameTop - st::mediaPlayerTimeFont->ascent);
	_playback->setGeometry(st::mediaPlayerPadding, st::mediaPlayerPlaybackTop, width() - 2 * st::mediaPlayerPadding, 2 * st::mediaPlayerPlaybackPadding + st::mediaPlayerPlaybackLine);
	_repeatTrack->moveToRight(st::mediaPlayerPlayLeft, st::mediaPlayerPlayTop);
	_volumeController->moveToRight(st::mediaPlayerVolumeRight, st::mediaPlayerPlayTop + (_playPause->height() - _volumeController->height()) / 2);
	updatePlayPrevNextPositions();
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::windowBg);
}

void CoverWidget::updatePlayPrevNextPositions() {
	_playPause->moveToLeft(st::mediaPlayerPlayLeft, st::mediaPlayerPlayTop);
}

} // namespace Player
} // namespace Media
