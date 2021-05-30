/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_raster.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_viewport_tile.h"
#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"
#include "styles/palette.h"

namespace Calls::Group {

Viewport::Renderer::Renderer(not_null<Viewport*> owner)
: _owner(owner)
, _pinIcon(st::groupCallLargeVideo.pin)
, _pinBackground(
	(st::groupCallLargeVideo.pinPadding.top()
		+ st::groupCallLargeVideo.pin.icon.height()
		+ st::groupCallLargeVideo.pinPadding.bottom()) / 2,
	st::radialBg) {
}

void Viewport::Renderer::paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) {
	auto bg = clip;
	auto hq = PainterHighQualityEnabler(p);
	const auto bounding = clip.boundingRect();
	const auto opengl = (backend == Ui::GL::Backend::OpenGL);
	for (const auto &tile : _owner->_tiles) {
		paintTile(p, tile.get(), bounding, opengl, bg);
	}
	for (const auto rect : bg) {
		p.fillRect(rect, st::groupCallBg);
	}
}

void Viewport::Renderer::paintTile(
		Painter &p,
		not_null<VideoTile*> tile,
		const QRect &clip,
		bool opengl,
		QRegion &bg) {
	const auto track = tile->track();
	const auto data = track->frameWithInfo(true);
	const auto &image = data.original;
	const auto rotation = data.rotation;
	if (image.isNull()) {
		return;
	}

	const auto fill = [&](QRect rect) {
		const auto intersected = rect.intersected(clip);
		if (!intersected.isEmpty()) {
			p.fillRect(intersected, st::groupCallMembersBg);
			bg -= intersected;
		}
	};

	using namespace Media::View;
	const auto geometry = tile->geometry();
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto scaled = FlipSizeByRotation(
		image.size(),
		rotation
	).scaled(QSize(width, height), Qt::KeepAspectRatio);
	const auto left = (width - scaled.width()) / 2;
	const auto top = (height - scaled.height()) / 2;
	const auto target = QRect(QPoint(x + left, y + top), scaled);
	if (UsePainterRotation(rotation, opengl)) {
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
	bg -= target;
	track->markFrameShown();

	if (left > 0) {
		fill({ x, y, left, height });
	}
	if (const auto right = left + scaled.width(); right < width) {
		fill({ x + right, y, width - right, height });
	}
	if (top > 0) {
		fill({ x, y, width, top });
	}
	if (const auto bottom = top + scaled.height(); bottom < height) {
		fill({ x, y + bottom, width, height - bottom });
	}

	paintTileControls(p, x, y, width, height, tile);
}

void Viewport::Renderer::paintTileControls(
		Painter &p,
		int x,
		int y,
		int width,
		int height,
		not_null<VideoTile*> tile) {
	p.setClipRect(x, y, width, height);
	const auto guard = gsl::finally([&] { p.setClipping(false); });

	// Pin.
	const auto wide = _owner->wide();
	if (wide) {
		const auto inner = tile->pinInner();
		VideoTile::PaintPinButton(
			p,
			tile->pinned(),
			x + inner.x(),
			y + inner.y(),
			_owner->widget()->width(),
			&_pinBackground,
			&_pinIcon);
	}

	const auto shown = _owner->_controlsShownRatio;
	if (shown == 0.) {
		return;
	}

	const auto &st = st::groupCallLargeVideo;
	const auto fullShift = st.namePosition.y() + st::normalFont->height;
	const auto shift = anim::interpolate(fullShift, 0, shown);

	// Shadow.
	if (_shadow.isNull()) {
		_shadow = GenerateShadow(st.shadowHeight, 0, kShadowMaxAlpha);
	}
	const auto shadowRect = QRect(
		x,
		y + (height - anim::interpolate(0, st.shadowHeight, shown)),
		width,
		st.shadowHeight);
	const auto shadowFill = shadowRect.intersected({ x, y, width, height });
	if (shadowFill.isEmpty()) {
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
	const auto row = tile->row();
	row->lazyInitialize(st::groupCallMembersListItem);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = x + width - st.iconPosition.x() - icon.width();
	const auto iconTop = y + (height
		- st.iconPosition.y()
		- icon.height()
		+ shift);
	row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::LargeVideo);

	// Name.
	p.setPen(st::groupCallVideoTextFg);
	const auto hasWidth = width
		- st.iconPosition.x() - icon.width()
		- st.namePosition.x();
	const auto nameLeft = x + st.namePosition.x();
	const auto nameTop = y + (height
		- st.namePosition.y()
		- st::semiboldFont->height
		+ shift);
	row->name().drawLeftElided(p, nameLeft, nameTop, hasWidth, width);
}

} // namespace Calls::Group
