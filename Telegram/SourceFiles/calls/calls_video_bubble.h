/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"
#include "ui/rp_widget.h"

namespace Webrtc {
class VideoTrack;
enum class VideoState;
} // namespace Webrtc

namespace Calls {

class VideoBubble final {
public:
	VideoBubble(
		not_null<QWidget*> parent,
		not_null<Webrtc::VideoTrack*> track);

	enum class DragMode {
		None,
		SnapToCorners,
	};
	void updateGeometry(
		DragMode mode,
		QRect boundingRect,
		QSize sizeMin = QSize(),
		QSize sizeMax = QSize());
	[[nodiscard]] QRect geometry() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _content.lifetime();
	}

	void setMirrored(bool mirrored) {
		_mirrored = mirrored;
	}

private:
	void setup();
	void paint();
	void setState(Webrtc::VideoState state);
	void applyDragMode(DragMode mode);
	void applyBoundingRect(QRect rect);
	void applySizeConstraints(QSize min, QSize max);
	void updateSizeToFrame(QSize frame);
	void updateVisibility();
	void setInnerSize(QSize size);
	void prepareFrame();

	Ui::RpWidget _content;
	const not_null<Webrtc::VideoTrack*> _track;
	Webrtc::VideoState _state = Webrtc::VideoState();
	QImage _frame, _pausedFrame;
	QSize _min, _max, _size, _lastDraggableSize, _lastFrameSize;
	QRect _boundingRect;
	DragMode _dragMode = DragMode::None;
	RectPart _corner = RectPart::None;
	bool _dragging = false;
	bool _geometryDirty = false;
	bool _mirrored = true;

};

} // namespace Calls
