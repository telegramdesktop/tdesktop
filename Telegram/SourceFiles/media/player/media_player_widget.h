/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

class AudioMsgId;

namespace Ui {
class FlatLabel;
class LabelSimple;
class IconButton;
class PlainShadow;
class FilledSlider;
} // namespace Ui

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace Clip
} // namespace Media

namespace Main {
class Session;
} // namespace Main

namespace Media {
namespace Player {

class PlayButton;
class VolumeWidget;
struct TrackState;

class Widget : public Ui::RpWidget, private base::Subscriber {
public:
	Widget(QWidget *parent, not_null<Main::Session*> session);

	void setCloseCallback(Fn<void()> callback);
	void setShowItemCallback(Fn<void(not_null<const HistoryItem*>)> callback);
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
	void updatePlaybackSpeedIcon();
	void createPrevNextButtons();
	void destroyPrevNextButtons();

	bool hasPlaybackSpeedControl() const;
	void updateVolumeToggleIcon();

	void checkForTypeChange();
	void setType(AudioMsgId::Type type);
	void handleSongUpdate(const TrackState &state);
	void handleSongChange();
	void handlePlaylistUpdate();

	void updateTimeText(const TrackState &state);
	void updateTimeLabel();

	const not_null<Main::Session*> _session;

	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;
	QString _time;

	// We display all the controls according to _type.
	// We switch to Type::Voice if a voice/video message is played.
	// We switch to Type::Song only if _voiceIsActive == false.
	// We change _voiceIsActive to false only manually or from tracksFinished().
	AudioMsgId::Type _type = AudioMsgId::Type::Unknown;
	AudioMsgId _lastSongId;
	bool _voiceIsActive = false;
	Fn<void()> _closeCallback;
	Fn<void(not_null<const HistoryItem*>)> _showItemCallback;

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
	object_ptr<Ui::IconButton> _playbackSpeed;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow = { nullptr };
	object_ptr<Ui::FilledSlider> _playbackSlider;
	std::unique_ptr<View::PlaybackProgress> _playbackProgress;

	rpl::lifetime _playlistChangesLifetime;

};

} // namespace Player
} // namespace Media
