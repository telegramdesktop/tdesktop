/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/video/video_editor_common.h"
#include "ui/rp_widget.h"

namespace Media::Streaming {
class Instance;
struct Update;
} // namespace Media::Streaming

namespace Ui {
class IconButton;
class LayerWidget;
class RoundButton;
class FlatLabel;
} // namespace Ui

namespace Editor {

class Crop;
class VideoTimeline;

struct VideoEditorDescriptor {
	QString path;
	QSize dimensions;
	crl::time duration = 0;
	VideoEditorData data;
};

class VideoEditor final : public Ui::RpWidget {
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
	void handleUpdate(Media::Streaming::Update &&update);
	void restart(crl::time position);
	void applyGeometry();
	void paint(QPainter &p);
	void keyPressEvent(QKeyEvent *e) override;

	const QString _path;
	const QSize _dimensions;
	const crl::time _duration = 0;
	const VideoEditorData _data;

	PhotoModifications _geometry;
	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _cover = 0;
	crl::time _position = 0;

	std::unique_ptr<Media::Streaming::Instance> _instance;
	base::unique_qptr<Crop> _crop;
	base::unique_qptr<VideoTimeline> _timeline;
	base::unique_qptr<Ui::RpWidget> _controls;
	base::unique_qptr<Ui::IconButton> _rotate;
	base::unique_qptr<Ui::IconButton> _flip;
	base::unique_qptr<Ui::RoundButton> _confirm;
	base::unique_qptr<Ui::RoundButton> _cancelButton;
	base::unique_qptr<Ui::FlatLabel> _about;

	QRect _frameRect;
	QTransform _frameMatrix;
	QRect _innerRect;
	QImage _lastFrame;
	bool _paused = false;

	rpl::event_stream<VideoModifications> _done;
	rpl::event_stream<> _cancel;

};

void InitVideoEditorLayer(
	not_null<Ui::LayerWidget*> layer,
	not_null<VideoEditor*> editor,
	Fn<void(VideoModifications)> doneCallback);

} // namespace Editor
