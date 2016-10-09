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
#include "media/media_audio.h"
#include "media/view/media_clip_playback.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_volume_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_mediaview.h"
#include "shortcuts.h"

namespace Media {
namespace Player {

using State = PlayButtonLayout::State;

class CoverWidget::PlayButton : public Button {
public:
	PlayButton(QWidget *parent);

	void setState(State state) {
		_layout.setState(state);
	}
	void finishTransform() {
		_layout.finishTransform();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	PlayButtonLayout _layout;

};

CoverWidget::PlayButton::PlayButton(QWidget *parent) : Button(parent)
, _layout(st::mediaPlayerButton, [this] { update(); }) {
	resize(st::mediaPlayerButtonSize);
	setCursor(style::cur_pointer);
}

void CoverWidget::PlayButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.translate(st::mediaPlayerButtonPosition.x(), st::mediaPlayerButtonPosition.y());
	_layout.paint(p, st::mediaPlayerActiveFg);
}

CoverWidget::CoverWidget(QWidget *parent) : TWidget(parent)
, _nameLabel(this, st::mediaPlayerName)
, _timeLabel(this, st::mediaPlayerTime)
, _playback(this, st::mediaPlayerPlayback)
, _playPause(this)
, _volumeController(this)
, _repeatTrack(this, st::mediaPlayerRepeatButton) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	_playback->setChangeProgressCallback([this](float64 value) {
		handleSeekProgress(value);
	});
	_playback->setChangeFinishedCallback([this](float64 value) {
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([this]() {
		if (exists()) {
			instance()->playPauseCancelClicked();
		}
	});

	updateRepeatTrackIcon();
	_repeatTrack->setClickedCallback([this]() {
		instance()->toggleRepeat();
		updateRepeatTrackIcon();
	});

	if (exists()) {
		subscribe(instance()->playlistChangedNotifier(), [this]() {
			handlePlaylistUpdate();
		});
		subscribe(instance()->updatedNotifier(), [this](const UpdatedEvent &e) {
			handleSongUpdate(e);
		});
		subscribe(instance()->songChangedNotifier(), [this]() {
			handleSongChange();
		});
		handleSongChange();
		if (auto player = audioPlayer()) {
			AudioMsgId playing;
			auto playbackState = player->currentState(&playing, AudioMsgId::Type::Song);
			handleSongUpdate(UpdatedEvent(&playing, &playbackState));
			_playPause->finishTransform();
		}
	}
}

void CoverWidget::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<int64>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabel();
		if (exists()) {
			instance()->startSeeking();
		}
	}
}

void CoverWidget::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<int64>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	_seekPositionMs = -1;

	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	if (playing && playbackState.duration) {
		audioPlayer()->seek(qRound(progress * playbackState.duration));
	}

	if (exists()) {
		instance()->stopSeeking();
	}
}

void CoverWidget::resizeEvent(QResizeEvent *e) {
	_nameLabel->resizeToWidth(width() - 2 * (st::mediaPlayerPadding) - _timeLabel->width() - st::normalFont->spacew);
	updateLabelPositions();

	int skip = (st::mediaPlayerPlayback.seekSize.width() / 2);
	int length = (width() - 2 * st::mediaPlayerPadding + st::mediaPlayerPlayback.seekSize.width());
	_playback->setGeometry(st::mediaPlayerPadding - skip, st::mediaPlayerPlaybackTop, length, 2 * st::mediaPlayerPlaybackPadding + st::mediaPlayerPlayback.width);

	_repeatTrack->moveToRight(st::mediaPlayerPlayLeft, st::mediaPlayerPlayTop);
	_volumeController->moveToRight(st::mediaPlayerVolumeRight, st::mediaPlayerVolumeTop);
	updatePlayPrevNextPositions();
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::windowBg);
}

void CoverWidget::updatePlayPrevNextPositions() {
	if (_previousTrack) {
		auto left = st::mediaPlayerPlayLeft;
		_previousTrack->moveToLeft(left, st::mediaPlayerPlayTop); left += _previousTrack->width() + st::mediaPlayerPlaySkip;
		_playPause->moveToLeft(left, st::mediaPlayerPlayTop); left += _playPause->width() + st::mediaPlayerPlaySkip;
		_nextTrack->moveToLeft(left, st::mediaPlayerPlayTop);
	} else {
		_playPause->moveToLeft(st::mediaPlayerPlayLeft, st::mediaPlayerPlayTop);
	}
}

void CoverWidget::updateLabelPositions() {
	_nameLabel->moveToLeft(st::mediaPlayerPadding, st::mediaPlayerNameTop - st::mediaPlayerName.font->ascent);
	_timeLabel->moveToRight(st::mediaPlayerPadding, st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);
}

void CoverWidget::updateRepeatTrackIcon() {
	_repeatTrack->setIcon(instance()->repeatEnabled() ? nullptr : &st::mediaPlayerRepeatDisabledIcon);
}

void CoverWidget::handleSongUpdate(const UpdatedEvent &e) {
	auto &audioId = *e.audioId;
	auto &playbackState = *e.playbackState;
	if (!audioId || !audioId.audio()->song()) {
		return;
	}

	_playback->updateState(*e.playbackState);

	auto stopped = ((playbackState.state & AudioPlayerStoppedMask) || playbackState.state == AudioPlayerFinishing);
	auto showPause = !stopped && (playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerResuming || playbackState.state == AudioPlayerStarting);
	if (exists() && instance()->isSeeking()) {
		showPause = true;
	}
	auto state = [audio = audioId.audio(), showPause] {
		if (audio->loading()) {
			return State::Cancel;
		} else if (showPause) {
			return State::Pause;
		}
		return State::Play;
	};
	_playPause->setState(state());

	updateTimeText(audioId, playbackState);
}

void CoverWidget::updateTimeText(const AudioMsgId &audioId, const AudioPlaybackState &playbackState) {
	QString time;
	qint64 position = 0, duration = 0, display = 0;
	auto frequency = (playbackState.frequency ? playbackState.frequency : AudioVoiceMsgFrequency);
	if (!(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
		display = position = playbackState.position;
		duration = playbackState.duration;
	} else {
		display = playbackState.duration ? playbackState.duration : (audioId.audio()->song()->duration * frequency);
	}

	_lastDurationMs = (playbackState.duration * 1000LL) / frequency;

	if (audioId.audio()->loading()) {
		auto loaded = audioId.audio()->loadOffset();
		auto loadProgress = snap(float64(loaded) / qMax(audioId.audio()->size, 1), 0., 1.);
		_time = QString::number(qRound(loadProgress * 100)) + '%';
		_playback->setDisabled(true);
	} else {
		display = display / frequency;
		_time = formatDurationText(display);
		_playback->setDisabled(false);
	}
	if (_seekPositionMs < 0) {
		updateTimeLabel();
	}
}

void CoverWidget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(formatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		_nameLabel->resizeToWidth(width() - 2 * (st::mediaPlayerPadding) - _timeLabel->width() - st::normalFont->spacew);
		updateLabelPositions();
	}
}

void CoverWidget::handleSongChange() {
	auto &current = instance()->current();
	auto song = current.audio()->song();

	TextWithEntities textWithEntities;
	if (song->performer.isEmpty()) {
		textWithEntities.text = song->title.isEmpty() ? (current.audio()->name.isEmpty() ? qsl("Unknown Track") : current.audio()->name) : song->title;
	} else {
		auto title = song->title.isEmpty() ? qsl("Unknown Track") : textClean(song->title);
		textWithEntities.text = song->performer + QString::fromUtf8(" \xe2\x80\x93 ") + title;
		textWithEntities.entities.append({ EntityInTextBold, 0, song->performer.size(), QString() });
	}
	_nameLabel->setMarkedText(textWithEntities);

	handlePlaylistUpdate();
}

void CoverWidget::handlePlaylistUpdate() {
	auto &current = instance()->current();
	auto &playlist = instance()->playlist();
	auto index = playlist.indexOf(current.contextId());
	if (!current || index < 0) {
		destroyPrevNextButtons();
	} else {
		createPrevNextButtons();
		auto previousEnabled = (index > 0);
		auto nextEnabled = (index + 1 < playlist.size());
		_previousTrack->setIcon(previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
		_previousTrack->setCursor(previousEnabled ? style::cur_pointer : style::cur_default);
		_nextTrack->setIcon(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setCursor(nextEnabled ? style::cur_pointer : style::cur_default);
	}
}

void CoverWidget::createPrevNextButtons() {
	if (!_previousTrack) {
		_previousTrack.create(this, st::mediaPlayerPreviousButton);
		_nextTrack.create(this, st::mediaPlayerNextButton);
		_previousTrack->setClickedCallback([this]() {
			if (exists()) {
				instance()->previous();
			}
		});
		_nextTrack->setClickedCallback([this]() {
			if (exists()) {
				instance()->next();
			}
		});
		updatePlayPrevNextPositions();
	}
}

void CoverWidget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
