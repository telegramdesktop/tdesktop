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
namespace {

constexpr auto kPausedVideoSize = 90;

} // namespace

Viewport::VideoTile::VideoTile(
	const VideoEndpoint &endpoint,
	VideoTileTrack track,
	rpl::producer<QSize> trackSize,
	rpl::producer<bool> pinned,
	Fn<void()> update)
: _endpoint(endpoint)
, _update(std::move(update))
, _track(track)
, _trackSize(std::move(trackSize)) {
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

QSize Viewport::VideoTile::PausedVideoSize() {
	return QSize(kPausedVideoSize, kPausedVideoSize);
}

QSize Viewport::VideoTile::trackOrUserpicSize() const {
	if (const auto size = trackSize(); !size.isEmpty()) {
		return size;
	} else if (_userpicSize.isEmpty()
		&& _track.track->state() == Webrtc::VideoState::Paused) {
		_userpicSize = PausedVideoSize();
	}
	return _userpicSize;
}

bool Viewport::VideoTile::screencast() const {
	return (_endpoint.type == VideoEndpointType::Screen);
}

void Viewport::VideoTile::setGeometry(
		QRect geometry,
		TileAnimation animation) {
	_hidden = false;
	_geometry = geometry;
	_animation = animation;
	updateTopControlsPosition();
}

void Viewport::VideoTile::hide() {
	_hidden = true;
	_quality = std::nullopt;
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
	if (_hidden) {
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

void Viewport::VideoTile::updateTopControlsSize() {
	const auto &st = st::groupCallVideoTile;

	const auto pinSize = PinInnerSize(_pinned);
	const auto pinWidth = st.pinPosition.x() * 2 + pinSize.width();
	const auto pinHeight = st.pinPosition.y() * 2 + pinSize.height();
	_pinInner = QRect(QPoint(), pinSize);
	_pinOuter = QRect(0, 0, pinWidth, pinHeight);

	const auto backSize = BackInnerSize();
	const auto backWidth = st.pinPosition.x() * 2 + backSize.width();
	const auto backHeight = st.pinPosition.y() * 2 + backSize.height();
	_backInner = QRect(QPoint(), backSize);
	_backOuter = QRect(0, 0, backWidth, backHeight);
}

void Viewport::VideoTile::updateTopControlsPosition() {
	const auto &st = st::groupCallVideoTile;

	_pinInner = QRect(
		_geometry.width() - st.pinPosition.x() - _pinInner.width(),
		st.pinPosition.y(),
		_pinInner.width(),
		_pinInner.height());
	_pinOuter = QRect(
		_geometry.width() - _pinOuter.width(),
		0,
		_pinOuter.width(),
		_pinOuter.height());
	_backInner = QRect(st.pinPosition, _backInner.size());
}

void Viewport::VideoTile::setup(rpl::producer<bool> pinned) {
	std::move(
		pinned
	) | rpl::filter([=](bool pinned) {
		return (_pinned != pinned);
	}) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		updateTopControlsSize();
		if (!_hidden) {
			updateTopControlsPosition();
			_update();
		}
	}, _lifetime);

	_track.track->renderNextFrame(
	) | rpl::start_with_next(_update, _lifetime);

	updateTopControlsSize();
}

} // namespace Calls::Group
