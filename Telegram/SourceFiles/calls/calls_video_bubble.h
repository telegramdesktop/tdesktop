/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace webrtc {
class VideoTrack;
enum class VideoState;
} // namespace webrtc

namespace Calls {

class VideoBubble final {
public:
	VideoBubble(
		not_null<QWidget*> parent,
		not_null<webrtc::VideoTrack*> track);

	enum class DragMode {
		None,
		SnapToCorners,
	};
	void updateGeometry(
		DragMode mode,
		QRect boundingRect,
		QSize sizeMin = QSize(),
		QSize sizeMax = QSize());

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _content.lifetime();
	}

private:
	void setup();
	void paint();
	void setState(webrtc::VideoState state);
	void applyDragMode(DragMode mode);
	void applyBoundingRect(QRect rect);
	void applySizeConstraints(QSize min, QSize max);
	void updateSizeToFrame(QSize frame);
	void updateVisibility();
	void setInnerSize(QSize size);
	void prepareFrame();

	Ui::RpWidget _content;
	const not_null<webrtc::VideoTrack*> _track;
	webrtc::VideoState _state = webrtc::VideoState();
	QImage _frame, _pausedFrame;
	QSize _min, _max, _size, _lastDraggableSize, _lastFrameSize;
	QRect _boundingRect;
	DragMode _dragMode = DragMode::None;
	RectPart _corner = RectPart::None;
	bool _dragging = false;
	bool _geometryDirty = false;

};

} // namespace Calls
