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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "boxes/addcontactbox.h"
#include "application.h"
#include "window.h"
#include "playerwidget.h"
#include "mainwidget.h"

#include "localstorage.h"

#include "audio.h"

PlayerWidget::PlayerWidget(QWidget *parent) : TWidget(parent)
, _prevAvailable(false)
, _nextAvailable(false)
, _fullAvailable(false)
, _over(OverNone)
, _down(OverNone)
, _downCoord(0)
, _downFrequency(AudioVoiceMsgFrequency)
, _downProgress(0.)
, _stateAnim(animFunc(this, &PlayerWidget::stateStep))
, _msgmigrated(false)
, _index(-1)
, _migrated(0)
, _history(0)
, _timeWidth(0)
, _repeat(false)
, _showPause(false)
, _position(0)
, _duration(0)
, _loaded(0)
, a_progress(0., 0.)
, a_loadProgress(0., 0.)
, _progressAnim(animFunc(this, &PlayerWidget::progressStep))
, _sideShadow(this, st::shadowColor) {
	resize(st::wndMinWidth, st::playerHeight);
	setMouseTracking(true);
	memset(_stateHovers, 0, sizeof(_stateHovers));
	_sideShadow.setVisible(cWideMode());
}

void PlayerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect()), checkr(myrtlrect(r));
	p.fillRect(r, st::playerBg->b);

	if (!_playbackRect.contains(checkr)) {
		if (_fullAvailable && checkr.intersects(_prevRect)) {
			if (_prevAvailable) {
				float64 o = _stateHovers[OverPrev];
				p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			} else {
				p.setOpacity(st::playerUnavailableOpacity);
			}
			p.drawSpriteCenterLeft(_prevRect, width(), st::playerPrev);
		}
		if (checkr.intersects(_playRect)) {
			float64 o = _stateHovers[OverPlay];
			p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			p.drawSpriteCenterLeft(_playRect, width(), (_showPause || _down == OverPlayback) ? st::playerPause : st::playerPlay);
		}
		if (_fullAvailable && checkr.intersects(_nextRect)) {
			if (_nextAvailable) {
				float64 o = _stateHovers[OverNext];
				p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			} else {
				p.setOpacity(st::playerUnavailableOpacity);
			}
			p.drawSpriteCenterLeft(_nextRect, width(), st::playerNext);
		}
		if (checkr.intersects(_closeRect)) {
			float64 o = _stateHovers[OverClose];
			p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			p.drawSpriteCenterLeft(_closeRect, width(), st::playerClose);
		}
		if (checkr.intersects(_volumeRect)) {
			float64 o = _stateHovers[OverVolume];
			p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			int32 top = _volumeRect.y() + (_volumeRect.height() - st::playerVolume.pxHeight()) / 2;
			int32 left = _volumeRect.x() + (_volumeRect.width() - st::playerVolume.pxWidth()) / 2;
			int32 mid = left + qRound(st::playerVolume.pxWidth() * cSongVolume());
			int32 right = left + st::playerVolume.pxWidth();
			if (rtl()) {
				left = width() - left;
				mid = width() - mid;
				right = width() - right;
				if (mid < left) {
					p.drawPixmap(QRect(mid, top, left - mid, st::playerVolume.pxHeight()), App::sprite(), QRect(st::playerVolume.x() + (mid - right) * cIntRetinaFactor(), st::playerVolume.y(), (left - mid) * cIntRetinaFactor(), st::playerVolume.pxHeight() * cIntRetinaFactor()));
				}
				if (right < mid) {
					p.setOpacity(st::playerUnavailableOpacity);
					p.drawPixmap(QRect(right, top, mid - right, st::playerVolume.pxHeight()), App::sprite(), QRect(st::playerVolume.x(), st::playerVolume.y(), (mid - right) * cIntRetinaFactor(), st::playerVolume.pxHeight() * cIntRetinaFactor()));
				}
			} else {
				if (mid > left) {
					p.drawPixmap(QRect(left, top, mid - left, st::playerVolume.pxHeight()), App::sprite(), QRect(st::playerVolume.x(), st::playerVolume.y(), (mid - left) * cIntRetinaFactor(), st::playerVolume.pxHeight() * cIntRetinaFactor()));
				}
				if (right > mid) {
					p.setOpacity(st::playerUnavailableOpacity);
					p.drawPixmap(QRect(mid, top, right - mid, st::playerVolume.pxHeight()), App::sprite(), QRect(st::playerVolume.x() + (mid - left) * cIntRetinaFactor(), st::playerVolume.y(), (right - mid) * cIntRetinaFactor(), st::playerVolume.pxHeight() * cIntRetinaFactor()));
				}
			}
		}
		if (_fullAvailable && checkr.intersects(_fullRect)) {
			float64 o = _stateHovers[OverFull];
			p.setOpacity(o * 1. + (1. - o) * st::playerInactiveOpacity);
			p.drawSpriteCenterLeft(_fullRect, width(), st::playerFull);
		}
		if (checkr.intersects(_repeatRect)) {
			float64 o = _stateHovers[OverRepeat];
			p.setOpacity(_repeat ? 1. : (o * st::playerInactiveOpacity + (1. - o) * st::playerUnavailableOpacity));
			p.drawSpriteCenterLeft(_repeatRect, width(), st::playerRepeat);
		}
		p.setOpacity(1.);

		p.setPen(st::playerTimeFg->p);
		p.setFont(st::linkFont->f);
		p.drawTextLeft(_infoRect.x() + _infoRect.width() - _timeWidth, _infoRect.y() + (_infoRect.height() - st::linkFont->height) / 2, width(), _time, _timeWidth);

		textstyleSet(&st::playerNameStyle);
		p.setPen(st::playerFg->p);
		_name.drawElided(p, _infoRect.x() + (rtl() ? (_timeWidth + st::playerSkip) : 0), _infoRect.y() + (_infoRect.height() - st::linkFont->height) / 2, _infoRect.width() - _timeWidth - st::playerSkip);
		textstyleRestore();
	}

	if (_duration) {
		float64 prg = (_down == OverPlayback) ? _downProgress : a_progress.current();
		int32 from = _playbackRect.x(), mid = qRound(_playbackRect.x() + prg * _playbackRect.width()), end = _playbackRect.x() + _playbackRect.width();
		if (mid > from) {
			p.fillRect(rtl() ? (width() - mid) : from, height() - st::playerLineHeight, mid - from, _playbackRect.height(), st::playerLineActive->b);
		}
		if (end > mid) {
			p.fillRect(rtl() ? (width() - end) : mid, height() - st::playerLineHeight, end - mid, st::playerLineHeight, st::playerLineInactive->b);
		}
		if (_stateHovers[OverPlayback] > 0) {
			p.setOpacity(_stateHovers[OverPlayback]);

			int32 x = mid - (st::playerMoverSize.width() / 2);
			p.fillRect(rtl() ? (width() - x - st::playerMoverSize.width()) : x, height() - st::playerMoverSize.height(), st::playerMoverSize.width(), st::playerMoverSize.height(), st::playerLineActive->b);
		}
	} else if (a_loadProgress.current() > 0) {
		int32 from = _playbackRect.x(), mid = qRound(_playbackRect.x() + a_loadProgress.current() * _playbackRect.width());
		if (mid > from) {
			p.fillRect(rtl() ? (width() - mid) : from, height() - st::playerLineHeight, mid - from, _playbackRect.height(), st::playerLineInactive->b);
		}
	}
}

void PlayerWidget::mousePressEvent(QMouseEvent *e) {
	QPoint pos(myrtlpoint(e->pos()));

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (_song && _over == OverPlay) {
			playPausePressed();
			return;
		} else if (_over == OverPrev) {
			prevPressed();
		} else if (_over == OverNext) {
			nextPressed();
		} else if (_over == OverClose) {
			_down = OverClose;
		} else if (_over == OverVolume) {
			_down = OverVolume;
			_downCoord = pos.x() - _volumeRect.x();
			cSetSongVolume(snap((_downCoord - ((_volumeRect.width() - st::playerVolume.pxWidth()) / 2)) / float64(st::playerVolume.pxWidth()), 0., 1.));
			emit audioPlayer()->songVolumeChanged();
			rtlupdate(_volumeRect);
		} else if (_over == OverPlayback) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			if (playing == _song && playingDuration) {
				if (playingState == AudioPlayerPlaying || playingState == AudioPlayerStarting || playingState == AudioPlayerResuming) {
					audioPlayer()->pauseresume(OverviewDocuments);
				}
				_down = OverPlayback;
				_downProgress = snap((pos.x() - _playbackRect.x()) / float64(_playbackRect.width()), 0., 1.);
				_downDuration = playingDuration;
				_downFrequency = (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);

				rtlupdate(_playbackRect);
				updateDownTime();
			}
		} else if (_over == OverFull && _song) {
			if (HistoryItem *item = App::histItemById(_song.msgId)) {
				App::main()->showMediaOverview(item->history()->peer, OverviewAudioDocuments);
			}
		} else if (_over == OverRepeat) {
			_repeat = !_repeat;
			updateOverRect(OverRepeat);
		}
	}
}

void PlayerWidget::updateDownTime() {
	QString time = formatDurationText(qRound(_downDuration * _downProgress) / _downFrequency);
	if (time != _time) {
		_time = time;
		_timeWidth = st::linkFont->width(_time);
		rtlupdate(_infoRect);
	}
}

void PlayerWidget::updateOverState(OverState newState) {
	bool result = true;
	if (_over != newState) {
		updateOverRect(_over);
		updateOverRect(newState);
		if (_over != OverNone) {
			_stateAnimations.remove(_over);
			_stateAnimations[-_over] = getms() - ((1. - _stateHovers[_over]) * st::playerDuration);
			if (!_stateAnim.animating()) _stateAnim.start();
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_stateAnimations.remove(-_over);
			_stateAnimations[_over] = getms() - (_stateHovers[_over] * st::playerDuration);
			if (!_stateAnim.animating()) _stateAnim.start();
			setCursor(style::cur_pointer);
		} else {
			setCursor(style::cur_default);
		}
	}
}

void PlayerWidget::updateOverRect(OverState state) {
	switch (state) {
	case OverPrev: rtlupdate(_prevRect); break;
	case OverPlay: rtlupdate(_playRect); break;
	case OverNext: rtlupdate(_nextRect); break;
	case OverClose: rtlupdate(_closeRect); break;
	case OverVolume: rtlupdate(_volumeRect); break;
	case OverFull: rtlupdate(_fullRect); break;
	case OverRepeat: rtlupdate(_repeatRect); break;
	case OverPlayback: rtlupdate(_playbackRect); break;
	}
}

void PlayerWidget::updateControls() {
	_fullAvailable = (_index >= 0);

	History *history = _msgmigrated ? _migrated : _history;
	_prevAvailable = _fullAvailable && ((_index > 0) || (_index == 0 && _migrated && !_msgmigrated && !_migrated->overview[OverviewAudioDocuments].isEmpty()));
	_nextAvailable = _fullAvailable && ((_index < history->overview[OverviewAudioDocuments].size() - 1) || (_msgmigrated && _index == _migrated->overview[OverviewAudioDocuments].size() - 1 && _history->overviewLoaded(OverviewAudioDocuments) && _history->overviewCount(OverviewAudioDocuments) > 0));
	resizeEvent(0);
	update();
	if (_index >= 0 && _index < MediaOverviewStartPerPage) {
		if (!_history->overviewLoaded(OverviewAudioDocuments) || (_migrated && !_migrated->overviewLoaded(OverviewAudioDocuments))) {
			if (App::main()) {
				if (_msgmigrated || (_migrated && _index == 0 && _history->overviewLoaded(OverviewAudioDocuments))) {
					App::main()->loadMediaBack(_migrated->peer, OverviewAudioDocuments);
				} else {
					App::main()->loadMediaBack(_history->peer, OverviewAudioDocuments);
					if (_migrated && _index == 0 && _migrated->overview[OverviewAudioDocuments].isEmpty() && !_migrated->overviewLoaded(OverviewAudioDocuments)) {
						App::main()->loadMediaBack(_migrated->peer, OverviewAudioDocuments);
					}
				}
				if (_msgmigrated && !_history->overviewCountLoaded(OverviewAudioDocuments)) {
					App::main()->preloadOverview(_history->peer, OverviewAudioDocuments);
				}
			}
		}
	}
}

void PlayerWidget::findCurrent() {
	_index = -1;
	if (!_history) return;

	const History::MediaOverview *o = &(_msgmigrated ? _migrated : _history)->overview[OverviewAudioDocuments];
	if ((_msgmigrated ? _migrated : _history)->channelId() == _song.msgId.channel) {
		for (int i = 0, l = o->size(); i < l; ++i) {
			if (o->at(i) == _song.msgId.msg) {
				_index = i;
				break;
			}
		}
	}
	preloadNext();
}

void PlayerWidget::preloadNext() {
	if (_index < 0) return;

	History *history = _msgmigrated ? _migrated : _history;
	const History::MediaOverview *o = &history->overview[OverviewAudioDocuments];
	HistoryItem *next = 0;
	if (_index < o->size() - 1) {
		next = App::histItemById(history->channelId(), o->at(_index + 1));
	} else if (_msgmigrated && _index == o->size() - 1 && _history->overviewLoaded(OverviewAudioDocuments) && _history->overviewCount(OverviewAudioDocuments) > 0) {
		next = App::histItemById(_history->channelId(), _history->overview[OverviewAudioDocuments].at(0));
	} else if (_msgmigrated && _index == o->size() - 1 && !_history->overviewCountLoaded(OverviewAudioDocuments)) {
		if (App::main()) App::main()->preloadOverview(_history->peer, OverviewAudioDocuments);
	}
	if (next) {
		if (HistoryDocument *document = static_cast<HistoryDocument*>(next->getMedia())) {
			if (document->document()->location(true).isEmpty() && document->document()->data.isEmpty()) {
				if (!document->document()->loader) {
					DocumentOpenLink::doOpen(document->document());
					document->document()->openOnSave = 0;
					document->document()->openOnSaveMsgId = FullMsgId();
				}
			}
		}
	}
}

void PlayerWidget::startPlay(const FullMsgId &msgId) {
	if (HistoryItem *item = App::histItemById(msgId)) {
		if (HistoryDocument *doc = static_cast<HistoryDocument*>(item->getMedia())) {
			audioPlayer()->play(SongMsgId(doc->document(), item->fullId()));
			updateState();
		}
	}
}

void PlayerWidget::clearSelection() {
	for (StateAnimations::const_iterator i = _stateAnimations.cbegin(); i != _stateAnimations.cend(); ++i) {
		_stateHovers[qAbs(i.key())] = 0;
	}
	_stateAnimations.clear();
}

void PlayerWidget::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (_history && (_history->peer == peer || (_migrated && _migrated->peer == peer)) && type == OverviewAudioDocuments) {
		_index = -1;
		History *history = _msgmigrated ? _migrated : _history;
		if (history->channelId() == _song.msgId.channel) {
			for (int i = 0, l = history->overview[OverviewAudioDocuments].size(); i < l; ++i) {
				if (history->overview[OverviewAudioDocuments].at(i) == _song.msgId.msg) {
					_index = i;
					preloadNext();
					break;
				}
			}
		}
		updateControls();
	}
}

void PlayerWidget::updateWideMode() {
	_sideShadow.setVisible(cWideMode());
}

bool PlayerWidget::seekingSong(const SongMsgId &song) const {
	return (_down == OverPlayback) && (song == _song);
}

bool PlayerWidget::stateStep(float64 msc) {
	bool result = false;
	uint64 ms = getms();
	for (StateAnimations::iterator i = _stateAnimations.begin(); i != _stateAnimations.cend();) {
		int32 over = qAbs(i.key());
		updateOverRect(OverState(over));

		float64 dt = float64(ms - i.value()) / st::playerDuration;
		if (dt >= 1) {
			_stateHovers[over] = (i.key() > 0) ? 1 : 0;
			i = _stateAnimations.erase(i);
		} else {
			_stateHovers[over] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	return !_stateAnimations.isEmpty();
}

void PlayerWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void PlayerWidget::leaveEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void PlayerWidget::updateSelected() {
	QPoint pos(myrtlpoint(mapFromGlobal(_lastMousePos)));

	if (_down == OverVolume) {
		int32 delta = (pos.x() - _volumeRect.x()) - _downCoord;
		float64 startFrom = snap((_downCoord - ((_volumeRect.width() - st::playerVolume.pxWidth()) / 2)) / float64(st::playerVolume.pxWidth()), 0., 1.);
		float64 add = delta / float64(4 * st::playerVolume.pxWidth()), result = snap(startFrom + add, 0., 1.);
		if (result != cSongVolume()) {
			cSetSongVolume(result);
			emit audioPlayer()->songVolumeChanged();
			rtlupdate(_volumeRect);
		}
	} else if (_down == OverPlayback) {
		_downProgress = snap((pos.x() - _playbackRect.x()) / float64(_playbackRect.width()), 0., 1.);
		rtlupdate(_playbackRect);
		updateDownTime();
	} else if (_down == OverNone) {
		bool inInfo = ((pos.x() >= _infoRect.x()) && (pos.x() < _fullRect.x() + _fullRect.width()) && (pos.y() >= _playRect.y()) && (pos.y() <= _playRect.y() + _playRect.height()));
		if (_prevAvailable && _prevRect.contains(pos)) {
			updateOverState(OverPrev);
		} else if (_nextAvailable && _nextRect.contains(pos)) {
			updateOverState(OverNext);
		} else if (_playRect.contains(pos)) {
			updateOverState(OverPlay);
		} else if (_closeRect.contains(pos)) {
			updateOverState(OverClose);
		} else if (_volumeRect.contains(pos)) {
			updateOverState(OverVolume);
		} else if (_repeatRect.contains(pos)) {
			updateOverState(OverRepeat);
		} else if (_duration && _playbackRect.contains(pos)) {
			updateOverState(OverPlayback);
		} else if (_fullAvailable && inInfo) {
			updateOverState(OverFull);
		} else if (_over != OverNone) {
			updateOverState(OverNone);
		}
	}
}

void PlayerWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_down == OverVolume) {
		mouseMoveEvent(e);
		Local::writeUserSettings();
	} else if (_down == OverPlayback) {
		mouseMoveEvent(e);
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		if (playing == _song && playingDuration) {
			_downDuration = playingDuration;
			audioPlayer()->seek(qRound(_downProgress * _downDuration));

			_showPause = true;

			a_progress = anim::fvalue(_downProgress, _downProgress);
			_progressAnim.stop();
		}
		update();
	} else if (_down == OverClose && _over == OverClose) {
		stopPressed();
	}
	_down = OverNone;
}

void PlayerWidget::playPressed() {
	if (!_song || isHidden()) return;

	SongMsgId playing;
	AudioPlayerState playingState = AudioPlayerStopped;
	audioPlayer()->currentState(&playing, &playingState);
	if (playing == _song && !(playingState & AudioPlayerStoppedMask)) {
		if (playingState == AudioPlayerPausing || playingState == AudioPlayerPaused || playingState == AudioPlayerPausedAtEnd) {
			audioPlayer()->pauseresume(OverviewDocuments);
		}
	} else {
		audioPlayer()->play(_song);
		if (App::main()) App::main()->documentPlayProgress(_song);
	}
}

void PlayerWidget::pausePressed() {
	if (!_song || isHidden()) return;

	SongMsgId playing;
	AudioPlayerState playingState = AudioPlayerStopped;
	audioPlayer()->currentState(&playing, &playingState);
	if (playing == _song && !(playingState & AudioPlayerStoppedMask)) {
		if (playingState == AudioPlayerStarting || playingState == AudioPlayerResuming || playingState == AudioPlayerPlaying || playingState == AudioPlayerFinishing) {
			audioPlayer()->pauseresume(OverviewDocuments);
		}
	}
}

void PlayerWidget::playPausePressed() {
	if (!_song || isHidden()) return;

	SongMsgId playing;
	AudioPlayerState playingState = AudioPlayerStopped;
	audioPlayer()->currentState(&playing, &playingState);
	if (playing == _song && !(playingState & AudioPlayerStoppedMask)) {
		audioPlayer()->pauseresume(OverviewDocuments);
	} else {
		audioPlayer()->play(_song);
		if (App::main()) App::main()->documentPlayProgress(_song);
	}
}

void PlayerWidget::prevPressed() {
	if (isHidden()) return;

	History *history = _msgmigrated ? _migrated : _history;
	const History::MediaOverview *o = history ? &history->overview[OverviewAudioDocuments] : 0;
	if (audioPlayer() && o && _index > 0 && _index <= o->size() && !o->isEmpty()) {
		startPlay(FullMsgId(history->channelId(), o->at(_index - 1)));
	} else if (!_index && _history && _migrated && !_msgmigrated) {
		o = &_migrated->overview[OverviewAudioDocuments];
		if (!o->isEmpty()) {
			startPlay(FullMsgId(_migrated->channelId(), o->at(o->size() - 1)));
		}
	}
}

void PlayerWidget::nextPressed() {
	if (isHidden()) return;

	History *history = _msgmigrated ? _migrated : _history;
	const History::MediaOverview *o = history ? &history->overview[OverviewAudioDocuments] : 0;
	if (audioPlayer() && o && _index >= 0 && _index < o->size() - 1) {
		startPlay(FullMsgId(history->channelId(), o->at(_index + 1)));
	} else if (o && (_index == o->size() - 1) && _msgmigrated && _history->overviewLoaded(OverviewAudioDocuments)) {
		o = &_history->overview[OverviewAudioDocuments];
		if (!o->isEmpty()) {
			startPlay(FullMsgId(_history->channelId(), o->at(0)));
		}
	}
}

void PlayerWidget::stopPressed() {
	if (!_song || isHidden()) return;

	audioPlayer()->stop(OverviewDocuments);
	if (App::main()) App::main()->hidePlayer();
}

void PlayerWidget::resizeEvent(QResizeEvent *e) {
	int32 availh = (height() - st::playerLineHeight);
	int32 ch = st::playerPlay.pxHeight() + st::playerSkip, ct = (availh - ch) / 2;
	_playbackRect = QRect(cWideMode() ? st::lineWidth : 0, height() - st::playerMoverSize.height(), width() - (cWideMode() ? st::lineWidth : 0), st::playerMoverSize.height());
	_prevRect = _fullAvailable ? QRect(st::playerSkip / 2, ct, st::playerPrev.pxWidth() + st::playerSkip, ch) : QRect();
	_playRect = QRect(_fullAvailable ? (_prevRect.x() + _prevRect.width()) : (st::playerSkip / 2), ct, st::playerPlay.pxWidth() + st::playerSkip, ch);
	_nextRect = _fullAvailable ? QRect(_playRect.x() + _playRect.width(), ct, st::playerNext.pxWidth() + st::playerSkip, ch) : QRect();

	_closeRect = QRect(width() - st::playerSkip / 2 - st::playerClose.pxWidth() - st::playerSkip, ct, st::playerClose.pxWidth() + st::playerSkip, ch);
	_volumeRect = QRect(_closeRect.x() - st::playerVolume.pxWidth() - st::playerSkip, ct, st::playerVolume.pxWidth() + st::playerSkip, ch);
	_repeatRect = QRect(_volumeRect.x() - st::playerRepeat.pxWidth() - st::playerSkip, ct, st::playerRepeat.pxWidth() + st::playerSkip, ch);
	_fullRect = _fullAvailable ? QRect(_repeatRect.x() - st::playerFull.pxWidth() - st::playerSkip, ct, st::playerFull.pxWidth() + st::playerSkip, ch) : QRect();

	int32 infoLeft = (_fullAvailable ? (_nextRect.x() + _nextRect.width()) : (_playRect.x() + _playRect.width()));
	_infoRect = QRect(infoLeft + st::playerSkip / 2, 0, (_fullAvailable ? _fullRect.x() : _repeatRect.x()) - infoLeft - st::playerSkip, availh);

	_sideShadow.resize(st::lineWidth, height());
	_sideShadow.moveToLeft(0, 0);

	update();
}

bool PlayerWidget::progressStep(float64 ms) {
	float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
	bool res = true;
	if (_duration && dt >= 1) {
		a_progress.finish();
		a_loadProgress.finish();
		res = false;
	} else {
		a_progress.update(qMin(dt, 1.), anim::linear);
		a_loadProgress.update(1. - (st::radialDuration / (st::radialDuration + ms)), anim::linear);
	}
	rtlupdate(_playbackRect);
	return res;
}

void PlayerWidget::updateState() {
	updateState(SongMsgId(), AudioPlayerStopped, 0, 0, 0);
}

void PlayerWidget::updateState(SongMsgId playing, AudioPlayerState playingState, int64 playingPosition, int64 playingDuration, int32 playingFrequency) {
	if (!playing) {
		audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
	}

	bool songChanged = false;
	if (playing && _song != playing) {
		songChanged = true;
		_song = playing;
		if (HistoryItem *item = App::histItemById(_song.msgId)) {
			_history = item->history();
			if (_history->peer->migrateFrom()) {
				_migrated = App::history(_history->peer->migrateFrom()->id);
				_msgmigrated = false;
			} else if (_history->peer->migrateTo()) {
				_migrated = _history;
				_history = App::history(_migrated->peer->migrateTo()->id);
				_msgmigrated = true;
			}
			findCurrent();
		} else {
			_history = 0;
			_msgmigrated = false;
			_index = -1;
		}
		SongData *song = _song.song->song();
		if (song->performer.isEmpty()) {
			_name.setText(st::linkFont, song->title.isEmpty() ? (_song.song->name.isEmpty() ? qsl("Unknown Track") : _song.song->name) : song->title, _textNameOptions);
		} else {
			TextCustomTagsMap custom;
			custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
			_name.setRichText(st::linkFont, QString::fromUtf8("[c]%1[/c] \xe2\x80\x93 %2").arg(textRichPrepare(song->performer)).arg(song->title.isEmpty() ? qsl("Unknown Track") : textRichPrepare(song->title)), _textNameOptions, custom);
		}
		updateControls();
	}

	qint64 position = 0, duration = 0, display = 0;
	if (playing == _song) {
		if (!(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
			display = position = playingPosition;
			duration = playingDuration;
		} else {
			display = playingDuration;
		}
		display = display / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
	} else if (_song) {
		display = _song.song->song()->duration;
	}
	bool showPause = false, stopped = ((playingState & AudioPlayerStoppedMask) || playingState == AudioPlayerFinishing);
	bool wasPlaying = !!_duration;
	if (!stopped) {
		showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
	}
	QString time;
	float64 progress = 0.;
	int32 loaded;
	float64 loadProgress = 1.;
	if (duration || !_song || !_song.song || !_song.song->loader) {
		time = (_down == OverPlayback) ? _time : formatDurationText(display);
		progress = duration ? snap(float64(position) / duration, 0., 1.) : 0.;
		loaded = duration ? _song.song->size : 0;
	} else {
		loaded = _song.song->loader ? _song.song->loader->currentOffset() : 0;
		time = formatDownloadText(loaded, _song.song->size);
		loadProgress = snap(float64(loaded) / qMax(_song.song->size, 1), 0., 1.);
	}
	if (time != _time || showPause != _showPause) {
		if (_time != time) {
			_time = time;
			_timeWidth = st::linkFont->width(_time);
		}
		_showPause = showPause;
		if (duration != _duration || position != _position || loaded != _loaded) {
			if (!songChanged && ((!stopped && duration && _duration) || (!duration && _loaded != loaded))) {
				a_progress.start(progress);
				a_loadProgress.start(loadProgress);
				_progressAnim.start();
			} else {
				a_progress = anim::fvalue(progress, progress);
				a_loadProgress = anim::fvalue(loadProgress, loadProgress);
				_progressAnim.stop();
			}
			_position = position;
			_duration = duration;
			_loaded = loaded;
		}
		update();
	} else if (duration != _duration || position != _position || loaded != _loaded) {
		if (!songChanged && ((!stopped && duration && _duration) || (!duration && _loaded != loaded))) {
			a_progress.start(progress);
			a_loadProgress.start(loadProgress);
			_progressAnim.start();
		} else {
			a_progress = anim::fvalue(progress, progress);
			a_loadProgress = anim::fvalue(loadProgress, loadProgress);
			_progressAnim.stop();
		}
		_position = position;
		_duration = duration;
		_loaded = loaded;
	}

	if (wasPlaying && playingState == AudioPlayerStoppedAtEnd) {
		if (_repeat) {
			startPlay(_song.msgId);
		} else {
			nextPressed();
		}
	}

	if (songChanged) {
		emit playerSongChanged(_song.msgId);
	}
}
