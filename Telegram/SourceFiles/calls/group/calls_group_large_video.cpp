/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_large_video.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
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
, _pinButton((_st.pinPosition.x() >= 0)
	? std::make_unique<Ui::AbstractButton>(&_content)
	: nullptr)
, _smallLayout(!_pinButton) {
	_content.setVisible(visible);
	if (_smallLayout) {
		_content.setCursor(style::cur_pointer);
	}
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
	if (width > 0 && height > 0) {
		const auto kMedium = style::ConvertScale(380);
		const auto kSmall = style::ConvertScale(200);
		_requestedQuality = (width > kMedium || height > kMedium)
			? VideoQuality::Full
			: (width > kSmall || height > kSmall)
			? VideoQuality::Medium
			: VideoQuality::Thumbnail;
	}
}

void LargeVideo::setControlsShown(float64 shown) {
	if (_controlsShownRatio == shown) {
		return;
	}
	_controlsShownRatio = shown;
	_content.update();
	updateControlsGeometry();
}

rpl::producer<bool> LargeVideo::pinToggled() const {
	return _pinButton
		? _pinButton->clicks() | rpl::map([=] { return !_pinned; })
		: rpl::never<bool>() | rpl::type_erased();
}

QSize LargeVideo::trackSize() const {
	return _trackSize.current();
}

rpl::producer<QSize> LargeVideo::trackSizeValue() const {
	return _trackSize.value();
}

rpl::producer<VideoQuality> LargeVideo::requestedQuality() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_content.shownValue(),
		_requestedQuality.value()
	) | rpl::filter([=](bool shown, auto) {
		return shown;
	}) | rpl::map(_2);
}

void LargeVideo::setup(
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned) {
	_content.setAttribute(Qt::WA_OpaquePaintEvent);

	_content.events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress
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
	if (_pinButton) {
		const auto &pin = st::groupCallLargeVideoPin.icon;
		const auto buttonWidth = pin.width() + 2 * _st.pinPosition.x();
		const auto buttonHeight = pin.height() + 2 * _st.pinPosition.y();
		_pinButton->setGeometry(
			_content.width() - buttonWidth,
			0,
			buttonWidth,
			buttonHeight);
	}
}

void LargeVideo::paint(QRect clip) {
	auto p = Painter(&_content);
	const auto fill = [&](QRect rect) {
		if (rect.intersects(clip)) {
			p.fillRect(rect.intersected(clip), st::groupCallMembersBg);
		}
	};
	const auto [image, rotation] = _track
		? _track.track->frameOriginalWithRotation()
		: std::pair<QImage, int>();
	if (image.isNull()) {
		fill(clip);
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
	if (UsePainterRotation(rotation, USE_OPENGL_LARGE_VIDEO)) {
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
	const auto shown = _controlsShownRatio;
	if (shown == 0.) {
		return;
	}

	const auto width = _content.width();
	const auto height = _content.height();
	const auto fullShift = _st.namePosition.y() + st::normalFont->height;
	const auto shift = anim::interpolate(fullShift, 0, shown);

	// Shadow.
	if (_shadow.isNull()) {
		_shadow = GenerateShadow(_st.shadowHeight, 0, kShadowMaxAlpha);
	}
	const auto shadowRect = QRect(
		0,
		(height - anim::interpolate(0, _st.shadowHeight, shown)),
		width,
		_st.shadowHeight);
	const auto shadowFill = shadowRect.intersected(clip);
	if (shadowFill.isEmpty() && _smallLayout) {
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
	_track.row->lazyInitialize(st::groupCallMembersListItem);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = width - _st.iconPosition.x() - icon.width();
	const auto iconTop = (height
		- _st.iconPosition.y()
		- icon.height()
		+ shift);
	_track.row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::LargeVideo);

	// Name.
	p.setPen(st::groupCallVideoTextFg);
	const auto hasWidth = width
		- _st.iconPosition.x() - icon.width()
		- _st.namePosition.x();
	const auto nameLeft = _st.namePosition.x();
	const auto nameTop = (height
		- _st.namePosition.y()
		- st::semiboldFont->height
		+ shift);
	_track.row->name().drawLeftElided(p, nameLeft, nameTop, hasWidth, width);

	// Pin.
	if (_pinButton) {
		const auto &pin = st::groupCallLargeVideoPin.icon;
		const auto pinLeft = (width - _st.pinPosition.x() - pin.width());
		const auto pinShift = anim::interpolate(
			_st.pinPosition.y() + pin.height(),
			0,
			shown);
		const auto pinTop = (_st.pinPosition.y() - pinShift);
		_pin.paint(p, pinLeft, pinTop, _pinned ? 1. : 0.);
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
