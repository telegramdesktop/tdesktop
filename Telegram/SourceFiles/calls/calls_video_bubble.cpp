/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_video_bubble.h"

#include "webrtc/webrtc_video_track.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/shadow.h"
#include "styles/style_calls.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"

namespace Calls {

VideoBubble::VideoBubble(
	not_null<QWidget*> parent,
	not_null<Webrtc::VideoTrack*> track)
: _content(parent)
, _track(track)
, _state(Webrtc::VideoState::Inactive) {
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
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
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

QRect VideoBubble::geometry() const {
	return _content.isHidden() ? QRect() : _content.geometry();
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

	prepareFrame();
	if (!_frame.isNull()) {
		const auto padding = st::boxRoundShadow.extend;
		const auto inner = _content.rect().marginsRemoved(padding);
		Ui::Shadow::paint(p, inner, _content.width(), st::boxRoundShadow);
		const auto factor = cIntRetinaFactor();
		p.drawImage(
			inner,
			_frame,
			QRect(
				QPoint(_frame.width() - (inner.width() * factor), 0),
				inner.size() * factor));
	}
	_track->markFrameShown();
}

void VideoBubble::prepareFrame() {
	const auto original = _track->frameSize();
	if (original.isEmpty()) {
		_frame = QImage();
		return;
	}
	const auto padding = st::boxRoundShadow.extend;
	const auto size = _content.rect().marginsRemoved(padding).size()
		* cIntRetinaFactor();

	// Should we check 'original' and 'size' aspect ratios?..
	const auto request = Webrtc::FrameRequest{
		.resize = size,
		.outer = size,
	};
	const auto frame = _track->frame(request);
	if (_frame.width() < size.width() || _frame.height() < size.height()) {
		_frame = QImage(
			size * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
	}
	Assert(_frame.width() >= frame.width()
		&& _frame.height() >= frame.height());
	const auto toPerLine = _frame.bytesPerLine();
	const auto fromPerLine = frame.bytesPerLine();
	const auto lineSize = frame.width() * 4;
	auto to = _frame.bits();
	auto from = frame.bits();
	const auto till = from + frame.height() * fromPerLine;
	for (; from != till; from += fromPerLine, to += toPerLine) {
		memcpy(to, from, lineSize);
	}
	Images::prepareRound(
		_frame,
		ImageRoundRadius::Large,
		RectPart::AllCorners,
		QRect(QPoint(), size));
	_frame = std::move(_frame).mirrored(true, false);
}

void VideoBubble::setState(Webrtc::VideoState state) {
	if (state == Webrtc::VideoState::Paused) {
		using namespace Images;
		static constexpr auto kRadius = 24;
		_pausedFrame = Images::BlurLargeImage(_track->frame({}), kRadius);
		if (_pausedFrame.isNull()) {
			state = Webrtc::VideoState::Inactive;
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
	const auto topLeft = [&] {
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
	}();
	const auto inner = QRect(topLeft, size);
	_content.setGeometry(inner.marginsAdded(st::boxRoundShadow.extend));
}

void VideoBubble::updateVisibility() {
	const auto size = _track->frameSize();
	const auto visible = (_state != Webrtc::VideoState::Inactive)
		&& !size.isEmpty();
	if (visible) {
		updateSizeToFrame(size);
	}
	_content.setVisible(visible);
}

} // namespace Calls
