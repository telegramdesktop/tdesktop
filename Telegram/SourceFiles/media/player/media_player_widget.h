/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_audio_msg_id.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

class AudioMsgId;

namespace Ui {
class FlatLabel;
class LabelSimple;
class IconButton;
class PlainShadow;
class FilledSlider;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace Clip
} // namespace Media

namespace Window {
class SessionController;
} // namespace Window

namespace Media {
namespace Player {

class PlayButton;
class SpeedButton;
class Dropdown;
struct TrackState;

class Widget final : public Ui::RpWidget, private base::Subscriber {
public:
	Widget(
		QWidget *parent,
		not_null<Ui::RpWidget*> dropdownsParent,
		not_null<Window::SessionController*> controller);

	void setCloseCallback(Fn<void()> callback);
	void setShowItemCallback(Fn<void(not_null<const HistoryItem*>)> callback);
	void stopAndClose();
	void setShadowGeometryToLeft(int x, int y, int w, int h);
	void hideShadowAndDropdowns();
	void showShadowAndDropdowns();
	void updateDropdownsGeometry();
	void raiseDropdowns();

	[[nodiscard]] rpl::producer<bool> togglePlaylistRequests() const {
		return _togglePlaylistRequests.events();
	}

	~Widget();

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	[[nodiscard]] not_null<Ui::RpWidget*> rightControls();
	void setupRightControls();

	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);

	[[nodiscard]] int getNameLeft() const;
	[[nodiscard]] int getNameRight() const;
	[[nodiscard]] int getTimeRight() const;
	void updateOverLabelsState(QPoint pos);
	void updateOverLabelsState(bool over);
	void hidePlaylistOn(not_null<Ui::RpWidget*> widget);

	void updatePlayPrevNextPositions();
	void updateLabelsGeometry();
	void updateRepeatToggleIcon();
	void updateControlsVisibility();
	void updateControlsGeometry();
	void updateControlsWrapGeometry();
	void updateControlsWrapVisibility();
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
	void markOver(bool over);

	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::RpWidget*> _orderMenuParent;

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
	rpl::event_stream<bool> _togglePlaylistRequests;
	bool _narrow = false;
	bool _over = false;
	bool _wontBeOver = false;
	bool _volumeHidden = false;

	class PlayButton;
	class OrderController;
	class SpeedController;
	object_ptr<Ui::FlatLabel> _nameLabel;
	object_ptr<Ui::FadeWrap<Ui::RpWidget>> _rightControls;
	object_ptr<Ui::LabelSimple> _timeLabel;
	object_ptr<Ui::IconButton> _previousTrack = { nullptr };
	object_ptr<Ui::IconButton> _playPause;
	object_ptr<Ui::IconButton> _nextTrack = { nullptr };
	object_ptr<Ui::IconButton> _volumeToggle;
	object_ptr<Ui::IconButton> _repeatToggle;
	object_ptr<Ui::IconButton> _orderToggle;
	object_ptr<Ui::IconButton> _speedToggle;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow = { nullptr };
	object_ptr<Ui::FilledSlider> _playbackSlider;
	base::unique_qptr<Dropdown> _volume;
	std::unique_ptr<View::PlaybackProgress> _playbackProgress;
	std::unique_ptr<OrderController> _orderController;
	std::unique_ptr<SpeedController> _speedController;

	rpl::lifetime _playlistChangesLifetime;

};

} // namespace Player
} // namespace Media
