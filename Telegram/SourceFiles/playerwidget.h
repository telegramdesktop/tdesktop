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

#include "audio.h"

class PlayerWidget : public TWidget {
	Q_OBJECT

public:

	PlayerWidget(QWidget *parent);

	void paintEvent(QPaintEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	void playPressed();
	void pausePressed();
	void playPausePressed();
	void prevPressed();
	void nextPressed();
	void stopPressed();
	void closePressed();

	void step_progress(float64 ms, bool timer);
	void step_state(uint64 ms, bool timer);

	void updateState(SongMsgId playing, AudioPlayerState playingState, int64 playingPosition, int64 playingDuration, int32 playingFrequency);
	void updateState();
	void clearSelection();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void updateAdaptiveLayout();

	bool seekingSong(const SongMsgId &song) const;

	void openPlayer();
	bool isOpened() const;
	void closePlayer();

	void showPlayer();
	void hidePlayer();

signals:

	void playerSongChanged(const FullMsgId &msgId);

private:

	// Use startPlayer()/stopPlayer() or showPlayer()/hidePlayer() instead.
	void show();
	void hide();

	enum OverState {
		OverNone = 0,
		OverPrev,
		OverPlay,
		OverNext,
		OverClose,
		OverVolume,
		OverFull,
		OverRepeat,
		OverPlayback,

		OverStateCount
	};
	void updateDownTime();
	void updateOverState(OverState newState);
	void updateOverRect(OverState state);

	void updateControls();
	void findCurrent();
	void preloadNext();

	void startPlay(const FullMsgId &msgId);

	QPoint _lastMousePos;
	void updateSelected();

	bool _playerOpened = false;

	bool _prevAvailable = false;
	bool _nextAvailable = false;
	bool _fullAvailable = false;
	OverState _over = OverNone;
	OverState _down = OverNone;
	int32 _downCoord = 0;
	int64 _downDuration;
	int32 _downFrequency = AudioVoiceMsgFrequency;
	float64 _downProgress = 0.;

	float64 _stateHovers[OverStateCount];
	typedef QMap<int32, uint64> StateAnimations;
	StateAnimations _stateAnimations;
	Animation _a_state;

	SongMsgId _song;
	bool _msgmigrated = false;
	int32 _index = -1;
	History *_migrated = nullptr;
	History *_history = nullptr;
	QRect _playRect, _prevRect, _nextRect, _playbackRect;
	QRect _closeRect, _volumeRect, _fullRect, _repeatRect, _infoRect;
	int32 _timeWidth = 0;
	bool _repeat = false;
	QString _time;
	Text _name;
	bool _showPause = false;
	int64 _position = 0;
	int64 _duration = 0;
	int32 _loaded = 0;

	anim::fvalue a_progress = { 0., 0. };
	anim::fvalue a_loadProgress = { 0., 0. };
	Animation _a_progress;

	PlainShadow _sideShadow;

};
