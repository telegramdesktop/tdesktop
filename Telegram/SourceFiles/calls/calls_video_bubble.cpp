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

void VideoBubble::setDragMode(DragMode mode) {
	if (_dragMode != mode) {
		applyDragMode(mode);
	}
}

void VideoBubble::setBoundingRect(QRect rect) {
	_boundingRect = rect;
	setSizeConstraints(rect.size());
}

void VideoBubble::applyDragMode(DragMode mode) {
	_dragMode = mode;
	if (_dragMode == DragMode::None) {
		_dragging = false;
		_content.setCursor(style::cur_default);
	}
	_content.setAttribute(
		Qt::WA_TransparentForMouseEvents,
		(_dragMode == DragMode::None));
}

void VideoBubble::setSizeConstraints(QSize min, QSize max) {
	Expects(!min.isEmpty());
	Expects(max.isEmpty() || min.width() <= max.width());
	Expects(max.isEmpty() || min.height() <= max.height());

	if (max.isEmpty()) {
		max = min;
	}
	applySizeConstraints(min, max);
}

void VideoBubble::applySizeConstraints(QSize min, QSize max) {
	_min = min;
	_max = max;
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

	auto size = _size;
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
	if (_size == size) {
		return;
	}
	_size = size;
	_content.setGeometry(
		_boundingRect.x() + (_boundingRect.width() - size.width()) / 2,
		_boundingRect.y() + (_boundingRect.height() - size.height()) / 2,
		size.width(),
		size.height());
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
