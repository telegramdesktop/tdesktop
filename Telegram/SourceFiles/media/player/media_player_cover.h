/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	using ButtonCallback = Fn<void()>;
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
