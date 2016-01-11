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

	void step_progress(float64 ms, bool timer);
	void step_state(uint64 ms, bool timer);

	void updateState(SongMsgId playing, AudioPlayerState playingState, int64 playingPosition, int64 playingDuration, int32 playingFrequency);
	void updateState();
	void clearSelection();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void updateWideMode();

	bool seekingSong(const SongMsgId &song) const;

signals:

	void playerSongChanged(const FullMsgId &msgId);

private:

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

	bool _prevAvailable, _nextAvailable, _fullAvailable;
	OverState _over, _down;
	int32 _downCoord;
	int64 _downDuration;
	int32 _downFrequency;
	float64 _downProgress;

	float64 _stateHovers[OverStateCount];
	typedef QMap<int32, uint64> StateAnimations;
	StateAnimations _stateAnimations;
	Animation _a_state;

	SongMsgId _song;
	bool _msgmigrated;
	int32 _index;
	History *_migrated, *_history;
	QRect _playRect, _prevRect, _nextRect, _playbackRect;
	QRect _closeRect, _volumeRect, _fullRect, _repeatRect, _infoRect;
	int32 _timeWidth;
	bool _repeat;
	QString _time;
	Text _name;
	bool _showPause;
	int64 _position, _duration;
	int32 _loaded;

	anim::fvalue a_progress, a_loadProgress;
	Animation _a_progress;

	PlainShadow _sideShadow;

};
