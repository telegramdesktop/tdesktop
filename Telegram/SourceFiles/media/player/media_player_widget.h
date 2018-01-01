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
class PlainShadow;
class FilledSlider;
} // namespace Ui

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {

class PlayButton;
class VolumeWidget;
struct TrackState;

class Widget : public Ui::RpWidget, private base::Subscriber {
public:
	Widget(QWidget *parent);

	void setCloseCallback(base::lambda<void()> callback);
	void stopAndClose();
	void setShadowGeometryToLeft(int x, int y, int w, int h);
	void showShadow();
	void hideShadow();

	QPoint getPositionForVolumeWidget() const;
	void volumeWidgetCreated(VolumeWidget *widget);

	~Widget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);

	int getLabelsLeft() const;
	int getLabelsRight() const;
	void updateOverLabelsState(QPoint pos);
	void updateOverLabelsState(bool over);

	void updatePlayPrevNextPositions();
	void updateLabelsGeometry();
	void updateRepeatTrackIcon();
	void createPrevNextButtons();
	void destroyPrevNextButtons();

	void updateVolumeToggleIcon();

	void checkForTypeChange();
	void setType(AudioMsgId::Type type);
	void handleSongUpdate(const TrackState &state);
	void handleSongChange();
	void handlePlaylistUpdate();

	void updateTimeText(const TrackState &state);
	void updateTimeLabel();

	TimeMs _seekPositionMs = -1;
	TimeMs _lastDurationMs = 0;
	QString _time;

	// We display all the controls according to _type.
	// We switch to Type::Voice if a voice/video message is played.
	// We switch to Type::Song only if _voiceIsActive == false.
	// We change _voiceIsActive to false only manually or from tracksFinished().
	AudioMsgId::Type _type = AudioMsgId::Type::Unknown;
	bool _voiceIsActive = false;
	base::lambda<void()> _closeCallback;

	bool _labelsOver = false;
	bool _labelsDown = false;

	class PlayButton;
	object_ptr<Ui::FlatLabel> _nameLabel;
	object_ptr<Ui::LabelSimple> _timeLabel;
	object_ptr<Ui::IconButton> _previousTrack = { nullptr };
	object_ptr<PlayButton> _playPause;
	object_ptr<Ui::IconButton> _nextTrack = { nullptr };
	object_ptr<Ui::IconButton> _volumeToggle;
	object_ptr<Ui::IconButton> _repeatTrack;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow = { nullptr };
	object_ptr<Ui::FilledSlider> _playbackSlider;
	std::unique_ptr<Clip::Playback> _playback;

	rpl::lifetime _playlistChangesLifetime;

};

} // namespace Clip
} // namespace Media
