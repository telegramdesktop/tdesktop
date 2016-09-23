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

#include "media/media_audio.h"
#include "ui/buttons/icon_button.h"
#include "ui/widgets/media_slider.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Media {
namespace Player {

VolumeController::VolumeController(QWidget *parent) : TWidget(parent)
, _toggle(this, st::mediaPlayerVolumeToggle)
, _slider(this, st::mediaPlayerPlayback) {
	_toggle->setClickedCallback([this]() {
		setVolume(_slider->value() ? 0. : _rememberedVolume);
	});
	_slider->setChangeProgressCallback([this](float64 volume) {
		applyVolumeChange(volume);
	});
	_slider->setChangeFinishedCallback([this](float64 volume) {
		if (volume > 0) {
			_rememberedVolume = volume;
		}
		applyVolumeChange(volume);
	});

	auto animated = false;
	setVolume(Global::SongVolume(), animated);

	resize(st::mediaPlayerVolumeWidth, 2 * st::mediaPlayerPlaybackPadding + st::mediaPlayerPlayback.width);
}

void VolumeController::resizeEvent(QResizeEvent *e) {
	_slider->resize(st::mediaPlayerVolumeLength, height());
	_slider->moveToRight(0, 0);
	_toggle->moveToLeft(0, (height() - _toggle->height()) / 2);
}

void VolumeController::setVolume(float64 volume, bool animated) {
	_slider->setValue(volume, animated);
	if (volume > 0) {
		_rememberedVolume = volume;
	}
	applyVolumeChange(volume);
}

void VolumeController::applyVolumeChange(float64 volume) {
	if (volume > 0) {
		if (volume < 1 / 3.) {
			_toggle->setIcon(&st::mediaPlayerVolumeIcon1);
		} else if (volume < 2 / 3.) {
			_toggle->setIcon(&st::mediaPlayerVolumeIcon2);
		} else {
			_toggle->setIcon(&st::mediaPlayerVolumeIcon3);
		}
	} else {
		_toggle->setIcon(nullptr);
	}
	if (volume != Global::SongVolume()) {
		Global::SetSongVolume(volume);
		if (auto player = audioPlayer()) {
			emit player->songVolumeChanged();
		}
	}
}

} // namespace Player
} // namespace Media
