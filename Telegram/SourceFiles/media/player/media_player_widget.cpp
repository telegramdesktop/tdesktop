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
#include "media/player/media_player_widget.h"

#include "ui/flatlabel.h"
#include "ui/widgets/label_simple.h"
#include "ui/widgets/filled_slider.h"
#include "ui/widgets/shadow.h"
#include "ui/buttons/icon_button.h"
#include "media/media_audio.h"
#include "media/view/media_clip_playback.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_volume_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_mediaview.h"

namespace Media {
namespace Player {

using State = PlayButtonLayout::State;

class Widget::PlayButton : public Button {
public:
	PlayButton(QWidget *parent);

	void setState(PlayButtonLayout::State state) {
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

Widget::PlayButton::PlayButton(QWidget *parent) : Button(parent)
, _layout(st::mediaPlayerButton, [this] { update(); }) {
	resize(st::mediaPlayerButtonSize);
	setCursor(style::cur_pointer);
}

void Widget::PlayButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.translate(st::mediaPlayerButtonPosition.x(), st::mediaPlayerButtonPosition.y());
	_layout.paint(p, st::mediaPlayerActiveFg);
}

Widget::Widget(QWidget *parent) : TWidget(parent)
, _nameLabel(this, st::mediaPlayerName)
, _timeLabel(this, st::mediaPlayerTime)
, _playPause(this)
, _volumeToggle(this, st::mediaPlayerVolumeToggle)
, _repeatTrack(this, st::mediaPlayerRepeatButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this, st::shadowColor)
, _playback(new Ui::FilledSlider(this, st::mediaPlayerPlayback)) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(st::wndMinWidth, st::mediaPlayerHeight + st::lineWidth);

	_playback->setChangeProgressCallback([this](float64 value) {
		handleSeekProgress(value);
	});
	_playback->setChangeFinishedCallback([this](float64 value) {
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([this] {
		if (exists()) {
			instance()->playPauseCancelClicked();
		}
	});

	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([this] {
		Global::SetSongVolume((Global::SongVolume() > 0) ? 0. : Global::RememberedSongVolume());
		Global::RefSongVolumeChanged().notify();
	});
	subscribe(Global::RefSongVolumeChanged(), [this] { updateVolumeToggleIcon(); });

	updateRepeatTrackIcon();
	_repeatTrack->setClickedCallback([this] {
		instance()->toggleRepeat();
	});

	if (exists()) {
		subscribe(instance()->repeatChangedNotifier(), [this] {
			updateRepeatTrackIcon();
		});
		subscribe(instance()->playlistChangedNotifier(), [this] {
			handlePlaylistUpdate();
		});
		subscribe(instance()->updatedNotifier(), [this](const UpdatedEvent &e) {
			handleSongUpdate(e);
		});
		subscribe(instance()->songChangedNotifier(), [this] {
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

void Widget::updateVolumeToggleIcon() {
	auto icon = []() -> const style::icon * {
		auto volume = Global::SongVolume();
		if (volume > 0) {
			if (volume < 1 / 3.) {
				return &st::mediaPlayerVolumeIcon1;
			} else if (volume < 2 / 3.) {
				return &st::mediaPlayerVolumeIcon2;
			}
			return &st::mediaPlayerVolumeIcon3;
		}
		return nullptr;
	};
	_volumeToggle->setIcon(icon());
}

void Widget::setCloseCallback(CloseCallback &&callback) {
	_close->setClickedCallback(std_::move(callback));
}

void Widget::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void Widget::showShadow() {
	_shadow->show();
	_playback->show();
}

void Widget::hideShadow() {
	_shadow->hide();
	_playback->hide();
}

QPoint Widget::getPositionForVolumeWidget() const {
	auto x = _volumeToggle->x();
	x += (_volumeToggle->width() - st::mediaPlayerVolumeSize.width()) / 2;
	if (rtl()) x = width() - x - st::mediaPlayerVolumeSize.width();
	return QPoint(x, height());
}

void Widget::volumeWidgetCreated(VolumeWidget *widget) {
	_volumeToggle->installEventFilter(widget);
}

void Widget::handleSeekProgress(float64 progress) {
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

void Widget::handleSeekFinished(float64 progress) {
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

void Widget::resizeEvent(QResizeEvent *e) {
	updatePlayPrevNextPositions();

	auto right = st::mediaPlayerCloseRight;
	_close->moveToRight(right, st::mediaPlayerPlayTop); right += _close->width();
	_repeatTrack->moveToRight(right, st::mediaPlayerPlayTop); right += _repeatTrack->width();
	_volumeToggle->moveToRight(right, st::mediaPlayerPlayTop); right += _volumeToggle->width();

	updateLabelsGeometry();

	_playback->setGeometry(0, height() - st::mediaPlayerPlayback.fullWidth, width(), st::mediaPlayerPlayback.fullWidth);
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto fill = e->rect().intersected(QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::windowBg);
	}
}

void Widget::updatePlayPrevNextPositions() {
	auto left = st::mediaPlayerPlayLeft;
	auto top = st::mediaPlayerPlayTop;
	if (_previousTrack) {
		_previousTrack->moveToLeft(left, top); left += _previousTrack->width() + st::mediaPlayerPlaySkip;
		_playPause->moveToLeft(left, top); left += _playPause->width() + st::mediaPlayerPlaySkip;
		_nextTrack->moveToLeft(left, top);
	} else {
		_playPause->moveToLeft(left, top);
	}
}

void Widget::updateLabelsGeometry() {
	auto left = st::mediaPlayerPlayLeft + _playPause->width();
	if (_previousTrack) {
		left += _previousTrack->width() + st::mediaPlayerPlaySkip + _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	left += st::mediaPlayerPadding;

	auto right = st::mediaPlayerCloseRight + _close->width() + _repeatTrack->width() + _volumeToggle->width();
	right += st::mediaPlayerPadding;

	auto widthForName = width() - left - right;
	widthForName -= _timeLabel->width() + 2 * st::normalFont->spacew;
	_nameLabel->resizeToWidth(widthForName);

	_nameLabel->moveToLeft(left, st::mediaPlayerNameTop - st::mediaPlayerName.font->ascent);
	_timeLabel->moveToRight(right, st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);
}

void Widget::updateRepeatTrackIcon() {
	_repeatTrack->setIcon(instance()->repeatEnabled() ? nullptr : &st::mediaPlayerRepeatDisabledIcon);
}

void Widget::handleSongUpdate(const UpdatedEvent &e) {
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

void Widget::updateTimeText(const AudioMsgId &audioId, const AudioPlaybackState &playbackState) {
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

void Widget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(formatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		updateLabelsGeometry();
	}
}

void Widget::handleSongChange() {
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

void Widget::handlePlaylistUpdate() {
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

void Widget::createPrevNextButtons() {
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

void Widget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
