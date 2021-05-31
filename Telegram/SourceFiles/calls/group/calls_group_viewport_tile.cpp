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
	VideoTileTrack track,
	rpl::producer<bool> pinned,
	Fn<void()> update)
: _endpoint(endpoint)
, _update(std::move(update))
, _track(track) {
	Expects(track.track != nullptr);
	Expects(track.row != nullptr);

	setup(std::move(pinned));
}

QRect Viewport::VideoTile::pinOuter() const {
	return _pinOuter;
}

QRect Viewport::VideoTile::pinInner() const {
	return _pinInner.translated(0, -topControlsSlide());
}

QRect Viewport::VideoTile::backOuter() const {
	return _backOuter;
}

QRect Viewport::VideoTile::backInner() const {
	return _backInner.translated(0, -topControlsSlide());
}

int Viewport::VideoTile::topControlsSlide() const {
	return anim::interpolate(
		st::groupCallVideoTile.pinPosition.y() + _pinInner.height(),
		0,
		_topControlsShownAnimation.value(_topControlsShown ? 1. : 0.));
}

bool Viewport::VideoTile::screencast() const {
	return (_endpoint.type == VideoEndpointType::Screen);
}

void Viewport::VideoTile::setGeometry(QRect geometry) {
	_geometry = geometry;
	updateTopControlsGeometry();
}

void Viewport::VideoTile::setShown(bool shown) {
	_shown = shown;
}

void Viewport::VideoTile::toggleTopControlsShown(bool shown) {
	if (_topControlsShown == shown) {
		return;
	}
	_topControlsShown = shown;
	_topControlsShownAnimation.start(
		_update,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration);
}

bool Viewport::VideoTile::updateRequestedQuality(VideoQuality quality) {
	if (!_shown) {
		_quality = std::nullopt;
		return false;
	} else if (_quality && *_quality == quality) {
		return false;
	}
	_quality = quality;
	return true;
}

QSize Viewport::VideoTile::PinInnerSize(bool pinned) {
	const auto &st = st::groupCallVideoTile;
	const auto &icon = st::groupCallVideoTile.pin.icon;
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
	const auto &st = st::groupCallVideoTile;
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
			+ st::groupCallVideoTile.pin.icon.width()
			+ st.pinTextPosition.x()),
		(y
			+ st.pinPadding.top()
			+ st.pinTextPosition.y()),
		outerWidth,
		(pinned
			? tr::lng_pinned_unpin(tr::now)
			: tr::lng_pinned_pin(tr::now)));

}

QSize Viewport::VideoTile::BackInnerSize() {
	const auto &st = st::groupCallVideoTile;
	const auto &icon = st::groupCallVideoTile.back;
	const auto innerWidth = icon.width()
		+ st.pinTextPosition.x()
		+ st::semiboldFont->width(tr::lng_create_group_back(tr::now));
	const auto innerHeight = icon.height();
	const auto buttonWidth = st.pinPadding.left()
		+ innerWidth
		+ st.pinPadding.right();
	const auto buttonHeight = st.pinPadding.top()
		+ innerHeight
		+ st.pinPadding.bottom();
	return { buttonWidth, buttonHeight };
}

void Viewport::VideoTile::PaintBackButton(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		not_null<Ui::RoundRect*> background) {
	const auto &st = st::groupCallVideoTile;
	const auto rect = QRect(QPoint(x, y), BackInnerSize());
	background->paint(p, rect);
	st.back.paint(
		p,
		rect.marginsRemoved(st.pinPadding).topLeft(),
		outerWidth);
	p.setPen(st::groupCallVideoTextFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(
		(x
			+ st.pinPadding.left()
			+ st::groupCallVideoTile.pin.icon.width()
			+ st.pinTextPosition.x()),
		(y
			+ st.pinPadding.top()
			+ st.pinTextPosition.y()),
		outerWidth,
		tr::lng_create_group_back(tr::now));
}

void Viewport::VideoTile::updateTopControlsGeometry() {
	const auto &st = st::groupCallVideoTile;

	const auto pinSize = PinInnerSize(_pinned);
	const auto pinWidth = st.pinPosition.x() * 2 + pinSize.width();
	const auto pinHeight = st.pinPosition.y() * 2 + pinSize.height();
	_pinInner = QRect(QPoint(), pinSize).translated(
		_geometry.width() - st.pinPosition.x() - pinSize.width(),
		st.pinPosition.y());
	_pinOuter = QRect(
		_geometry.width() - pinWidth,
		0,
		pinWidth,
		pinHeight);
	const auto backSize = BackInnerSize();
	const auto backWidth = st.pinPosition.x() * 2 + backSize.width();
	const auto backHeight = st.pinPosition.y() * 2 + backSize.height();
	_backInner = QRect(QPoint(), backSize).translated(st.pinPosition);
	_backOuter = QRect(0, 0, backWidth, backHeight);
}

void Viewport::VideoTile::setup(rpl::producer<bool> pinned) {
	std::move(
		pinned
	) | rpl::filter([=](bool pinned) {
		return (_pinned != pinned);
	}) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		updateTopControlsGeometry();
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

} // namespace Calls::Group
