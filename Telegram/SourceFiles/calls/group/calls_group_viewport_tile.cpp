/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_tile.h"

#include "webrtc/webrtc_video_track.h"
#include "lang/lang_keys.h"
#include "ui/round_rect.h"
#include "ui/effects/cross_line.h"
#include "styles/style_calls.h"

#include <QtGui/QOpenGLFunctions>

namespace Calls::Group {

Viewport::VideoTile::VideoTile(
	const VideoEndpoint &endpoint,
	LargeVideoTrack track,
	rpl::producer<bool> pinned,
	Fn<void()> update)
: _endpoint(endpoint)
, _update(std::move(update))
, _track(track) {
	Expects(track.track != nullptr);
	Expects(track.row != nullptr);

	setup(std::move(pinned));
}

Viewport::VideoTile::~VideoTile() {
	Expects(!_textures);
}

QRect Viewport::VideoTile::pinInner() const {
	return _pinInner.translated(0, -pinSlide());
}

QRect Viewport::VideoTile::pinOuter() const {
	return _pinOuter;
}

int Viewport::VideoTile::pinSlide() const {
	return anim::interpolate(
		st::groupCallLargeVideo.pinPosition.y() + _pinInner.height(),
		0,
		_pinShownAnimation.value(_pinShown ? 1. : 0.));
}

void Viewport::VideoTile::setGeometry(QRect geometry) {
	_geometry = geometry;
	updatePinnedGeometry();
}

void Viewport::VideoTile::togglePinShown(bool shown) {
	if (_pinShown == shown) {
		return;
	}
	_pinShown = shown;
	_pinShownAnimation.start(
		_update,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration);
}

bool Viewport::VideoTile::updateRequestedQuality(VideoQuality quality) {
	if (_quality && *_quality == quality) {
		return false;
	}
	_quality = quality;
	return true;
}

QSize Viewport::VideoTile::PinInnerSize(bool pinned) {
	const auto &st = st::groupCallLargeVideo;
	const auto &icon = st::groupCallLargeVideo.pin.icon;
	const auto innerWidth = icon.width()
		+ st.pinTextPosition.x()
		+ st::semiboldFont->width(pinned
			? tr::lng_pinned_unpin(tr::now)
			: tr::lng_pinned_pin(tr::now));
	const auto innerHeight = icon.height();
	const auto buttonWidth = st.pinPadding.left()
		+ innerWidth
		+ st.pinPadding.right();
	const auto buttonHeight = st.pinPadding.top()
		+ innerHeight
		+ st.pinPadding.bottom();
	return { buttonWidth, buttonHeight };
}

void Viewport::VideoTile::PaintPinButton(
		Painter &p,
		bool pinned,
		int x,
		int y,
		int outerWidth,
		not_null<Ui::RoundRect*> background,
		not_null<Ui::CrossLineAnimation*> icon) {
	const auto &st = st::groupCallLargeVideo;
	const auto rect = QRect(QPoint(x, y), PinInnerSize(pinned));
	background->paint(p, rect);
	icon->paint(
		p,
		rect.marginsRemoved(st.pinPadding).topLeft(),
		pinned ? 1. : 0.);
	p.setPen(st::groupCallVideoTextFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(
		(x
			+ st.pinPadding.left()
			+ st::groupCallLargeVideo.pin.icon.width()
			+ st.pinTextPosition.x()),
		(y
			+ st.pinPadding.top()
			+ st.pinTextPosition.y()),
		outerWidth,
		(pinned
			? tr::lng_pinned_unpin(tr::now)
			: tr::lng_pinned_pin(tr::now)));

}

void Viewport::VideoTile::updatePinnedGeometry() {
	const auto &st = st::groupCallLargeVideo;
	const auto buttonSize = PinInnerSize(_pinned);
	const auto fullWidth = st.pinPosition.x() * 2 + buttonSize.width();
	const auto fullHeight = st.pinPosition.y() * 2 + buttonSize.height();
	_pinInner = QRect(QPoint(), buttonSize).translated(
		_geometry.width() - st.pinPosition.x() - buttonSize.width(),
		st.pinPosition.y());
	_pinOuter = QRect(
		_geometry.width() - fullWidth,
		0,
		fullWidth,
		fullHeight);
}

void Viewport::VideoTile::setup(rpl::producer<bool> pinned) {
	std::move(
		pinned
	) | rpl::filter([=](bool pinned) {
		return (_pinned != pinned);
	}) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		updatePinnedGeometry();
		_update();
	}, _lifetime);

	_track.track->renderNextFrame(
	) | rpl::start_with_next([=] {
		const auto size = _track.track->frameSize();
		if (size.isEmpty()) {
			_track.track->markFrameShown();
		} else {
			_trackSize = size;
		}
		_update();
	}, _lifetime);

	if (const auto size = _track.track->frameSize(); !size.isEmpty()) {
		_trackSize = size;
	}
}

void Viewport::VideoTile::ensureTexturesCreated(
		QOpenGLFunctions &f) {
	_textures.values.ensureCreated(f);
}

const Viewport::Textures &Viewport::VideoTile::textures() const {
	return _textures;
}

Viewport::Textures Viewport::VideoTile::takeTextures() {
	return base::take(_textures);
}

} // namespace Calls::Group
