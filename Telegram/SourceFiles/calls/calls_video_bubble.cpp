/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_video_bubble.h"

#include "webrtc/webrtc_video_track.h"
#include "ui/image/image_prepare.h"
#include "styles/style_calls.h"

namespace Calls {

VideoBubble::VideoBubble(
	not_null<QWidget*> parent,
	not_null<webrtc::VideoTrack*> track)
: _content(parent)
, _track(track)
, _state(webrtc::VideoState::Inactive) {
	setup();
}

void VideoBubble::setup() {
	_content.show();
	applyDragMode(_dragMode);

	_content.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, lifetime());

	_track->stateValue(
	) | rpl::start_with_next([=](webrtc::VideoState state) {
		setState(state);
	}, lifetime());

	_track->renderNextFrame(
	) | rpl::start_with_next([=] {
		if (_track->frameSize().isEmpty()) {
			_track->markFrameShown();
		} else {
			updateVisibility();
			_content.update();
		}
	}, lifetime());
}

void VideoBubble::updateGeometry(
		DragMode mode,
		QRect boundingRect,
		QSize sizeMin,
		QSize sizeMax) {
	Expects(!boundingRect.isEmpty());
	Expects(sizeMax.isEmpty() || !sizeMin.isEmpty());
	Expects(sizeMax.isEmpty() || sizeMin.width() <= sizeMax.width());
	Expects(sizeMax.isEmpty() || sizeMin.height() <= sizeMax.height());

	if (sizeMin.isEmpty()) {
		sizeMin = boundingRect.size();
	}
	if (sizeMax.isEmpty()) {
		sizeMax = sizeMin;
	}
	if (_dragMode != mode) {
		applyDragMode(mode);
	}
	if (_boundingRect != boundingRect) {
		applyBoundingRect(boundingRect);
	}
	if (_min != sizeMin || _max != sizeMax) {
		applySizeConstraints(sizeMin, sizeMax);
	}
	if (_geometryDirty && !_lastFrameSize.isEmpty()) {
		updateSizeToFrame(base::take(_lastFrameSize));
	}
}

void VideoBubble::applyBoundingRect(QRect rect) {
	_boundingRect = rect;
	_geometryDirty = true;
}

void VideoBubble::applyDragMode(DragMode mode) {
	_dragMode = mode;
	if (_dragMode == DragMode::None) {
		_dragging = false;
		_content.setCursor(style::cur_default);
	}
	_content.setAttribute(
		Qt::WA_TransparentForMouseEvents,
		true/*(_dragMode == DragMode::None)*/);
	if (_dragMode == DragMode::SnapToCorners) {
		_corner = RectPart::BottomRight;
	} else {
		_corner = RectPart::None;
		_lastDraggableSize = _size;
	}
	_size = QSize();
	_geometryDirty = true;
}

void VideoBubble::applySizeConstraints(QSize min, QSize max) {
	_min = min;
	_max = max;
	_geometryDirty = true;
}

void VideoBubble::paint() {
	Painter p(&_content);

	auto hq = PainterHighQualityEnabler(p);
	p.drawImage(_content.rect(), _track->frame({}));
	_track->markFrameShown();
}

void VideoBubble::setState(webrtc::VideoState state) {
	if (state == webrtc::VideoState::Paused) {
		using namespace Images;
		static constexpr auto kRadius = 24;
		_pausedFrame = Images::BlurLargeImage(_track->frame({}), kRadius);
		if (_pausedFrame.isNull()) {
			state = webrtc::VideoState::Inactive;
		}
	}
	_state = state;
	updateVisibility();
}

void VideoBubble::updateSizeToFrame(QSize frame) {
	Expects(!frame.isEmpty());

	if (_lastFrameSize == frame) {
		return;
	}
	_lastFrameSize = frame;

	auto size = !_size.isEmpty()
		? _size
		: (_dragMode == DragMode::None || _lastDraggableSize.isEmpty())
		? QSize()
		: _lastDraggableSize;
	if (size.isEmpty()) {
		size = frame.scaled((_min + _max) / 2, Qt::KeepAspectRatio);
	} else {
		const auto area = size.width() * size.height();
		const auto w = int(std::round(std::max(
			std::sqrt((frame.width() * area) / (frame.height() * 1.)),
			1.)));
		const auto h = area / w;
		size = QSize(w, h);
	}
	size = QSize(std::max(1, size.width()), std::max(1, size.height()));
	setInnerSize(size);
}

void VideoBubble::setInnerSize(QSize size) {
	if (_size == size && !_geometryDirty) {
		return;
	}
	_geometryDirty = false;
	_size = size;
	_content.setGeometry(QRect([&] {
		switch (_corner) {
		case RectPart::None:
			return _boundingRect.topLeft() + QPoint(
				(_boundingRect.width() - size.width()) / 2,
				(_boundingRect.height() - size.height()) / 2);
		case RectPart::TopLeft:
			return _boundingRect.topLeft();
		case RectPart::TopRight:
			return QPoint(
				_boundingRect.x() + _boundingRect.width() - size.width(),
				_boundingRect.y());
		case RectPart::BottomRight:
			return QPoint(
				_boundingRect.x() + _boundingRect.width() - size.width(),
				_boundingRect.y() + _boundingRect.height() - size.height());
		case RectPart::BottomLeft:
			return QPoint(
				_boundingRect.x(),
				_boundingRect.y() + _boundingRect.height() - size.height());
		}
		Unexpected("Corner value in VideoBubble::setInnerSize.");
	}(), size));
}

void VideoBubble::updateVisibility() {
	const auto size = _track->frameSize();
	const auto visible = (_state != webrtc::VideoState::Inactive)
		&& !size.isEmpty();
	if (visible) {
		updateSizeToFrame(size);
	}
	_content.setVisible(visible);
}

} // namespace Calls
