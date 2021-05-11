/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_large_video.h"

#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/painter.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kShadowMaxAlpha = 80;

} // namespace

LargeVideo::LargeVideo(
	QWidget *parent,
	const style::GroupCallLargeVideo &st,
	bool visible,
	rpl::producer<LargeVideoTrack> track,
	rpl::producer<bool> pinned)
: _content(parent, [=](QRect clip) { paint(clip); })
, _st(st)
, _pin(st::groupCallLargeVideoPin)
, _pinButton(&_content)
, _minimizeButton((_st.controlsAlign == style::al_top)
	? std::make_unique<Ui::AbstractButton>(&_content)
	: nullptr)
, _controlsShown(_st.controlsAlign == style::al_top)
, _topControls(_st.controlsAlign == style::al_top)
, _controlsShownRatio(_controlsShown.current() ? 1. : 0.) {
	_content.setVisible(visible);
	setup(std::move(track), std::move(pinned));
}

void LargeVideo::raise() {
	_content.raise();
}

void LargeVideo::setVisible(bool visible) {
	_content.setVisible(visible);
}

void LargeVideo::setGeometry(int x, int y, int width, int height) {
	_content.setGeometry(x, y, width, height);
}

void LargeVideo::setControlsShown(bool shown) {
	if (_mouseInside == shown) {
		return;
	}
	_mouseInside = shown;
	if (!_toggleControlsScheduled) {
		_toggleControlsScheduled = true;
		crl::on_main(&_content, [=] { toggleControls(); });
	}
}

rpl::producer<bool> LargeVideo::pinToggled() const {
	return _pinButton.clicks() | rpl::map([=] { return !_pinned; });
}

rpl::producer<> LargeVideo::minimizeClicks() const {
	return _minimizeButton
		? (_minimizeButton->clicks() | rpl::to_empty)
		: (rpl::never<rpl::empty_value>() | rpl::type_erased());
}

rpl::producer<float64> LargeVideo::controlsShown() const {
	return _controlsShownRatio.value();
}

rpl::producer<QSize> LargeVideo::trackSizeValue() const {
	return _trackSize.value();
}

void LargeVideo::setup(
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned) {
	_content.setAttribute(Qt::WA_OpaquePaintEvent);

	_content.events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			Ui::Integration::Instance().registerLeaveSubscription(&_content);
			setControlsShown(true);
		} else if (e->type() == QEvent::Leave) {
			Ui::Integration::Instance().unregisterLeaveSubscription(
				&_content);
			setControlsShown(false);
		} else if (e->type() == QEvent::MouseButtonPress
			&& static_cast<QMouseEvent*>(
				e.get())->button() == Qt::LeftButton) {
			_mouseDown = true;
		} else if (e->type() == QEvent::MouseButtonRelease
			&& static_cast<QMouseEvent*>(
				e.get())->button() == Qt::LeftButton
			&& _mouseDown) {
			_mouseDown = false;
			if (!_content.isHidden()) {
				_clicks.fire({});
			}
		}
	}, _content.lifetime());

	rpl::combine(
		_content.shownValue(),
		std::move(track)
	) | rpl::map([=](bool shown, LargeVideoTrack track) {
		if (!shown) {
			_controlsAnimation.stop();
			if (!_topControls) {
				_controlsShown = _mouseInside = false;
			}
			_controlsShownRatio = _controlsShown.current() ? 1. : 0.;
		}
		return shown ? track : LargeVideoTrack();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](LargeVideoTrack track) {
		_track = track;
		_content.update();

		_trackLifetime.destroy();
		if (!track.track) {
			_trackSize = QSize();
			return;
		}
		track.track->renderNextFrame(
		) | rpl::start_with_next([=] {
			const auto size = track.track->frameSize();
			if (size.isEmpty()) {
				track.track->markFrameShown();
			} else {
				_trackSize = size;
			}
			_content.update();
		}, _trackLifetime);
		if (const auto size = track.track->frameSize(); !size.isEmpty()) {
			_trackSize = size;
		}
	}, _content.lifetime());

	setupControls(std::move(pinned));
}

void LargeVideo::toggleControls() {
	_toggleControlsScheduled = false;
	const auto shown = _mouseInside;
	if (_controlsShown.current() == shown) {
		return;
	}
	_controlsShown = shown;
	const auto callback = [=] {
		_controlsShownRatio = _controlsAnimation.value(
			_controlsShown.current() ? 1. : 0.);
		if (_topControls) {
			_content.update(0, 0, _content.width(), _st.shadowHeight);
		} else {
			_content.update();
		}
		updateControlsGeometry();
	};
	if (_content.isHidden()) {
		updateControlsGeometry();
	} else {
		_controlsAnimation.start(
			callback,
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::slideWrapDuration);
	}
}

void LargeVideo::setupControls(rpl::producer<bool> pinned) {
	std::move(pinned) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		_content.update();
	}, _content.lifetime());

	_content.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry();
	}, _content.lifetime());
}

void LargeVideo::updateControlsGeometry() {
	if (_topControls) {
		const auto &pin = st::groupCallLargeVideoPin.icon;
		const auto pinRight = (_content.width() - _st.pinPosition.x());
		const auto pinLeft = pinRight - pin.width();
		const auto pinTop = _st.pinPosition.y();
		const auto &icon = st::groupCallLargeVideoCrossLine.icon;
		const auto iconLeft = _content.width()
			- _st.iconPosition.x()
			- icon.width();
		const auto skip1 = iconLeft - pinRight;
		const auto &min = st::groupCallVideoMinimize;
		const auto minRight = _content.width() - _st.minimizePosition.x();
		const auto skip2 = pinLeft - minRight;
		_pinButton.setGeometry(
			pinLeft - (skip2 / 2),
			0,
			pin.width() + (skip2 / 2) + (skip1 / 2),
			pinTop * 2 + pin.height());
		_minimizeButton->setGeometry(
			minRight - min.width() - (skip2 / 2),
			0,
			min.width() + skip2,
			pinTop * 2 + pin.height());
	} else {
		_pinButton.setGeometry(
			0,
			_content.height() - _st.namePosition.y(),
			_st.namePosition.x(),
			_st.namePosition.y());
	}
}

void LargeVideo::paint(QRect clip) {
	auto p = Painter(&_content);
	const auto [image, rotation] = _track
		? _track.track->frameOriginalWithRotation()
		: std::pair<QImage, int>();
	if (image.isNull()) {
		p.fillRect(clip, Qt::black);
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	using namespace Media::View;
	const auto size = _content.size();
	const auto scaled = FlipSizeByRotation(
		image.size(),
		rotation
	).scaled(size, Qt::KeepAspectRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	const auto target = QRect(QPoint(left, top), scaled);
	if (UsePainterRotation(rotation)) {
		if (rotation) {
			p.save();
			p.rotate(rotation);
		}
		p.drawImage(RotatedRect(target, rotation), image);
		if (rotation) {
			p.restore();
		}
	} else if (rotation) {
		p.drawImage(target, RotateFrameImage(image, rotation));
	} else {
		p.drawImage(target, image);
	}
	_track.track->markFrameShown();

	const auto fill = [&](QRect rect) {
		if (rect.intersects(clip)) {
			p.fillRect(rect.intersected(clip), Qt::black);
		}
	};
	if (left > 0) {
		fill({ 0, 0, left, size.height() });
	}
	if (const auto right = left + scaled.width()
		; right < size.width()) {
		fill({ right, 0, size.width() - right, size.height() });
	}
	if (top > 0) {
		fill({ 0, 0, size.width(), top });
	}
	if (const auto bottom = top + scaled.height()
		; bottom < size.height()) {
		fill({ 0, bottom, size.width(), size.height() - bottom });
	}

	paintControls(p, clip);
}

void LargeVideo::paintControls(Painter &p, QRect clip) {
	const auto shown = _controlsShownRatio.current();
	if (shown == 0. && _topControls) {
		return;
	}

	const auto width = _content.width();
	const auto height = _content.height();
	const auto fullShift = _st.statusPosition.y() + st::normalFont->height;
	const auto shift = _topControls
		? anim::interpolate(-fullShift, 0, shown)
		: 0;

	// Shadow.
	if (_shadow.isNull()) {
		if (_topControls) {
			_shadow = GenerateShadow(_st.shadowHeight, kShadowMaxAlpha, 0);
		} else {
			_shadow = GenerateShadow(_st.shadowHeight, 0, kShadowMaxAlpha);
		}
	}
	const auto shadowRect = QRect(
		0,
		(_topControls
			? anim::interpolate(-_st.shadowHeight, 0, shown)
			: (height - anim::interpolate(_st.shadowHeight, 0, shown))),
		width,
		_st.shadowHeight);
	const auto shadowFill = shadowRect.intersected(clip);
	if (shadowFill.isEmpty() && (_topControls || shown == 0.)) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowFill,
		_shadow,
		QRect(
			0,
			(shadowFill.y() - shadowRect.y()) * factor,
			_shadow.width(),
			shadowFill.height() * factor));
	if (!_topControls && shown > 0.) {
		auto color = st::radialBg->c;
		color.setAlphaF(color.alphaF() * shown);
		p.fillRect(clip, color);

		p.setOpacity(shown);
		st::groupCallVideoEnlarge.paintInCenter(p, _content.rect());
		p.setOpacity(1.);
	}

	_track.row->lazyInitialize(st::groupCallMembersListItem);

	// Name.
	p.setPen(_topControls
		? st::groupCallVideoTextFg
		: st::groupCallVideoSubTextFg);
	const auto hasWidth = width
		- (_topControls ? _st.pinPosition.x() : _st.iconPosition.x())
		- _st.namePosition.x();
	const auto nameLeft = _st.namePosition.x();
	const auto nameTop = _topControls
		? (_st.namePosition.y() + shift)
		: (height - _st.namePosition.y());
	_track.row->name().drawLeftElided(p, nameLeft, nameTop, hasWidth, width);

	// Status.
	p.setPen(st::groupCallVideoSubTextFg);
	const auto statusLeft = _st.statusPosition.x();
	const auto statusTop = _topControls
		? (_st.statusPosition.y() + shift)
		: (height - _st.statusPosition.y());
	_track.row->paintComplexStatusText(
		p,
		st::groupCallLargeVideoListItem,
		statusLeft,
		statusTop,
		hasWidth,
		width,
		false,
		MembersRowStyle::LargeVideo);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = width - _st.iconPosition.x() - icon.width();
	const auto iconTop = _topControls
		? (_st.iconPosition.y() + shift)
		: (height - _st.iconPosition.y() - icon.height());
	_track.row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::LargeVideo);

	// Pin.
	const auto &pin = st::groupCallLargeVideoPin.icon;
	const auto pinLeft = _topControls
		? (width - _st.pinPosition.x() - pin.width())
		: _st.pinPosition.x();
	const auto pinTop = _topControls
		? (_st.pinPosition.y() + shift)
		: (height - _st.pinPosition.y() - pin.height());
	_pin.paint(p, pinLeft, pinTop, _pinned ? 1. : 0.);

	// Minimize.
	if (_topControls) {
		const auto &min = st::groupCallVideoMinimize;
		const auto minLeft = width - _st.minimizePosition.x() - min.width();
		const auto minTop = _st.minimizePosition.y() + shift;
		min.paint(p, minLeft, minTop, width);
	}
}

QImage GenerateShadow(int height, int topAlpha, int bottomAlpha) {
	Expects(topAlpha >= 0 && topAlpha < 256);
	Expects(bottomAlpha >= 0 && bottomAlpha < 256);
	Expects(height * style::DevicePixelRatio() < 65536);

	auto result = QImage(
		QSize(1, height * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	if (topAlpha == bottomAlpha) {
		result.fill(QColor(0, 0, 0, topAlpha));
		return result;
	}
	constexpr auto kShift = 16;
	constexpr auto kMultiply = (1U << kShift);
	const auto values = std::abs(topAlpha - bottomAlpha);
	const auto rows = uint32(result.height());
	const auto step = (values * kMultiply) / (rows - 1);
	const auto till = rows * uint32(step);
	Assert(result.bytesPerLine() == sizeof(uint32));
	auto ints = reinterpret_cast<uint32*>(result.bits());
	if (topAlpha < bottomAlpha) {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = ((topAlpha + (i >> kShift)) << 24);
		}
	} else {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = ((topAlpha - (i >> kShift)) << 24);
		}
	}
	return result;
}

} // namespace Calls::Group
