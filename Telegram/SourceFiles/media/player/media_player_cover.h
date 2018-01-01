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
#pragma once

#include "ui/rp_widget.h"

class AudioMsgId;

namespace Ui {
class FlatLabel;
class LabelSimple;
class IconButton;
class MediaSlider;
} // namespace Ui

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {

class VolumeController;
struct TrackState;

class CoverWidget : public Ui::RpWidget, private base::Subscriber {
public:
	CoverWidget(QWidget *parent);

	using ButtonCallback = base::lambda<void()>;
	void setPinCallback(ButtonCallback &&callback);
	void setCloseCallback(ButtonCallback &&callback);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setCloseVisible(bool visible);
	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);

	void updatePlayPrevNextPositions();
	void updateLabelPositions();
	void updateRepeatTrackIcon();
	void createPrevNextButtons();
	void destroyPrevNextButtons();

	void updateVolumeToggleIcon();

	void handleSongUpdate(const TrackState &state);
	void handleSongChange();
	void handlePlaylistUpdate();

	void updateTimeText(const TrackState &state);
	void updateTimeLabel();

	TimeMs _seekPositionMs = -1;
	TimeMs _lastDurationMs = 0;
	QString _time;

	class PlayButton;
	object_ptr<Ui::FlatLabel> _nameLabel;
	object_ptr<Ui::LabelSimple> _timeLabel;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::MediaSlider> _playbackSlider;
	std::unique_ptr<Clip::Playback> _playback;
	object_ptr<Ui::IconButton> _previousTrack = { nullptr };
	object_ptr<PlayButton> _playPause;
	object_ptr<Ui::IconButton> _nextTrack = { nullptr };
	object_ptr<Ui::IconButton> _volumeToggle;
	object_ptr<VolumeController> _volumeController;
	object_ptr<Ui::IconButton> _pinPlayer;
	object_ptr<Ui::IconButton> _repeatTrack;

};

} // namespace Clip
} // namespace Media
