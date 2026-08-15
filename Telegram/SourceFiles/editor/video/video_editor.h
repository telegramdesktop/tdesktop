/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "editor/video/video_editor_common.h"
#include "ui/rp_widget.h"

namespace Media::Streaming {
class Instance;
struct Update;
} // namespace Media::Streaming

namespace Ui {
class IconButton;
class LayerWidget;
class RippleButton;
class FlatLabel;
} // namespace Ui

namespace Editor {

class Crop;
class VideoTimeline;
class VideoQualitySlider;

struct VideoEditorDescriptor {
	QString path;
	QSize dimensions;
	crl::time duration = 0;
	VideoEditorData data;

	VideoModifications initial;
};

class VideoEditor final
	: public Ui::RpWidget
	, public base::has_weak_ptr {
public:
	VideoEditor(
		not_null<QWidget*> parent,
		VideoEditorDescriptor descriptor);
	~VideoEditor();

	[[nodiscard]] rpl::producer<VideoModifications> doneRequests() const {
		return _done.events();
	}
	[[nodiscard]] rpl::producer<> cancelRequests() const {
		return _cancel.events();
	}

	[[nodiscard]] VideoModifications collect() const;

private:
	void setupStreaming();
	void setupControls();
	void setupTimeline();
	void setupQuality();
	void refreshQualityLevels();
	void handleUpdate(Media::Streaming::Update &&update);
	void restart(crl::time position);
	void setupTapToPause();
	void updateBubble();
	void startCapture();
	[[nodiscard]] QRect captureFrom() const;
	[[nodiscard]] QSize bubbleSize() const;
	void refreshCoverPreview();
	void invalidateCoverPreview();
	void togglePause();
	[[nodiscard]] bool held() const;
	void paintPlayBadge(QPainter &p);
	void applyGeometry();
	void paint(QPainter &p);
	void keyPressEvent(QKeyEvent *e) override;

	const QString _path;
	const QSize _dimensions;
	const crl::time _duration = 0;
	const VideoEditorData _data;

	const VideoModifications _initial;

	PhotoModifications _geometry;
	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _cover = 0;
	crl::time _position = 0;
	bool _gif = false;

	std::unique_ptr<Media::Streaming::Instance> _instance;
	base::unique_qptr<Crop> _crop;
	base::unique_qptr<VideoTimeline> _timeline;
	base::unique_qptr<VideoQualitySlider> _quality;
	base::unique_qptr<Ui::RpWidget> _controls;
	base::unique_qptr<Ui::RpWidget> _bar;
	base::unique_qptr<Ui::IconButton> _rotate;
	base::unique_qptr<Ui::IconButton> _flip;
	base::unique_qptr<Ui::IconButton> _gifButton;
	base::unique_qptr<Ui::RippleButton> _confirm;
	base::unique_qptr<Ui::RippleButton> _cancelButton;
	base::unique_qptr<Ui::FlatLabel> _about;
	base::unique_qptr<Ui::FlatLabel> _hint;
	base::unique_qptr<Ui::RpWidget> _bubble;
	base::unique_qptr<Ui::RpWidget> _capture;
	Ui::Animations::Simple _captureShown;
	QImage _bubblePreview;

	QRect _frameRect;
	QTransform _frameMatrix;
	QRect _innerRect;
	QImage _lastFrame;
	bool _dragging = false;
	bool _userPaused = false;
	crl::time _bubbleCover = -1;
	bool _bubbleBusy = false;
	bool _bubbleVisible = false;
	bool _draggingHead = false;
	crl::time _bubbleShownAt = 0;
	base::Timer _bubbleHideTimer;
	Ui::Animations::Simple _bubbleShown;

	rpl::event_stream<VideoModifications> _done;
	rpl::event_stream<> _cancel;

};

void InitVideoEditorLayer(
	not_null<Ui::LayerWidget*> layer,
	not_null<VideoEditor*> editor,
	Fn<void(VideoModifications)> doneCallback);

} // namespace Editor
