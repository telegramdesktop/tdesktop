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
#include "data/data_peer.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"
#include "styles/palette.h"

namespace Calls::Group {
namespace {

constexpr auto kBlurRadius = 15;

} // namespace

Viewport::RendererSW::RendererSW(not_null<Viewport*> owner)
: _owner(owner)
, _pinIcon(st::groupCallVideoTile.pin)
, _pinBackground(
	(st::groupCallVideoTile.pinPadding.top()
		+ st::groupCallVideoTile.pin.icon.height()
		+ st::groupCallVideoTile.pinPadding.bottom()) / 2,
	st::radialBg) {
}

void Viewport::RendererSW::paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) {
	auto bg = clip;
	auto hq = PainterHighQualityEnabler(p);
	const auto bounding = clip.boundingRect();
	for (auto &[tile, tileData] : _tileData) {
		tileData.stale = true;
	}
	for (const auto &tile : _owner->_tiles) {
		if (!tile->visible()) {
			continue;
		}
		paintTile(p, tile.get(), bounding, bg);
	}
	for (const auto rect : bg) {
		p.fillRect(rect, st::groupCallBg);
	}
	for (auto i = _tileData.begin(); i != _tileData.end();) {
		if (i->second.stale) {
			i = _tileData.erase(i);
		} else {
			++i;
		}
	}
}

void Viewport::RendererSW::validateUserpicFrame(
		not_null<VideoTile*> tile,
		TileData &data) {
	if (!_userpicFrame) {
		data.userpicFrame = QImage();
		return;
	} else if (!data.userpicFrame.isNull()) {
		return;
	}
	auto userpic = QImage(
		tile->trackOrUserpicSize(),
		QImage::Format_ARGB32_Premultiplied);
	userpic.fill(Qt::black);
	{
		auto p = Painter(&userpic);
		tile->row()->peer()->paintUserpicSquare(
			p,
			tile->row()->ensureUserpicView(),
			0,
			0,
			userpic.width());
	}
	data.userpicFrame = Images::BlurLargeImage(
		std::move(userpic),
		kBlurRadius);
}

void Viewport::RendererSW::paintTile(
		Painter &p,
		not_null<VideoTile*> tile,
		const QRect &clip,
		QRegion &bg) {
	const auto track = tile->track();
	const auto markGuard = gsl::finally([&] {
		tile->track()->markFrameShown();
	});
	const auto data = track->frameWithInfo(true);
	auto &tileData = _tileData[tile];
	tileData.stale = false;
	_userpicFrame = (data.format == Webrtc::FrameFormat::None);
	_pausedFrame = (track->state() == Webrtc::VideoState::Paused);
	validateUserpicFrame(tile, tileData);
	if (_userpicFrame || !_pausedFrame) {
		tileData.blurredFrame = QImage();
	} else if (tileData.blurredFrame.isNull()) {
		tileData.blurredFrame = Images::BlurLargeImage(
			data.original.scaled(
				VideoTile::PausedVideoSize(),
				Qt::KeepAspectRatio),
			kBlurRadius);
	}
	const auto &image = _userpicFrame
		? tileData.userpicFrame
		: _pausedFrame
		? tileData.blurredFrame
		: data.original;
	const auto frameRotation = _userpicFrame ? 0 : data.rotation;
	Assert(!image.isNull());

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
		frameRotation
	).scaled(QSize(width, height), Qt::KeepAspectRatio);
	const auto left = (width - scaled.width()) / 2;
	const auto top = (height - scaled.height()) / 2;
	const auto target = QRect(QPoint(x + left, y + top), scaled);
	if (UsePainterRotation(frameRotation)) {
		if (frameRotation) {
			p.save();
			p.rotate(frameRotation);
		}
		p.drawImage(RotatedRect(target, frameRotation), image);
		if (frameRotation) {
			p.restore();
		}
	} else if (frameRotation) {
		p.drawImage(target, RotateFrameImage(image, frameRotation));
	} else {
		p.drawImage(target, image);
	}
	bg -= target;

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
	paintTileOutline(p, x, y, width, height, tile);
}

void Viewport::RendererSW::paintTileOutline(
		Painter &p,
		int x,
		int y,
		int width,
		int height,
		not_null<VideoTile*> tile) {
	if (!tile->row()->speaking()) {
		return;
	}
	const auto outline = st::groupCallOutline;
	const auto &color = st::groupCallMemberActiveIcon;
	p.setPen(Qt::NoPen);
	p.fillRect(x, y, outline, height - outline, color);
	p.fillRect(x + outline, y, width - outline, outline, color);
	p.fillRect(
		x + width - outline,
		y + outline,
		outline,
		height - outline,
		color);
	p.fillRect(x, y + height - outline, width - outline, outline, color);
}

void Viewport::RendererSW::paintTileControls(
		Painter &p,
		int x,
		int y,
		int width,
		int height,
		not_null<VideoTile*> tile) {
	p.setClipRect(x, y, width, height);
	const auto guard = gsl::finally([&] { p.setClipping(false); });

	const auto wide = _owner->wide();
	if (wide) {
		// Pin.
		const auto pinInner = tile->pinInner();
		VideoTile::PaintPinButton(
			p,
			tile->pinned(),
			x + pinInner.x(),
			y + pinInner.y(),
			_owner->widget()->width(),
			&_pinBackground,
			&_pinIcon);

		// Back.
		const auto backInner = tile->backInner();
		VideoTile::PaintBackButton(
			p,
			x + backInner.x(),
			y + backInner.y(),
			_owner->widget()->width(),
			&_pinBackground);
	}

	const auto &st = st::groupCallVideoTile;
	const auto nameTop = y + (height
		- st.namePosition.y()
		- st::semiboldFont->height);

	if (_pausedFrame) {
		p.fillRect(x, y, width, height, QColor(0, 0, 0, kShadowMaxAlpha));

		const auto middle = (st::groupCallVideoPlaceholderHeight
			- st::groupCallPaused.height()) / 2;
		const auto pausedSpace = (nameTop - y)
			- st::groupCallPaused.height()
			- st::semiboldFont->height;
		const auto pauseIconSkip = middle - st::groupCallVideoPlaceholderIconTop;
		const auto pauseTextSkip = st::groupCallVideoPlaceholderTextTop
			- st::groupCallVideoPlaceholderIconTop;
		const auto pauseIconTop = !_owner->wide()
			? (y + (height - st::groupCallPaused.height()) / 2)
			: (pausedSpace < 3 * st::semiboldFont->height)
			? (pausedSpace / 3)
			: std::min(
				y + (height / 2) - pauseIconSkip,
				(nameTop
					- st::semiboldFont->height * 3
					- st::groupCallPaused.height()));
		const auto pauseTextTop = (pausedSpace < 3 * st::semiboldFont->height)
			? (nameTop - (pausedSpace / 3) - st::semiboldFont->height)
			: std::min(
				pauseIconTop + pauseTextSkip,
				nameTop - st::semiboldFont->height * 2);

		st::groupCallPaused.paint(
			p,
			x + (width - st::groupCallPaused.width()) / 2,
			pauseIconTop,
			width);
		if (_owner->wide()) {
			p.drawText(
				QRect(x, pauseTextTop, width, y + height - pauseTextTop),
				tr::lng_group_call_video_paused(tr::now),
				style::al_top);
		}
	}

	const auto shown = _owner->_controlsShownRatio;
	if (shown == 0.) {
		return;
	}

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
	if (!_pausedFrame) {
		p.drawImage(
			shadowFill,
			_shadow,
			QRect(
				0,
				(shadowFill.y() - shadowRect.y()) * factor,
				_shadow.width(),
				shadowFill.height() * factor));
	}
	const auto row = tile->row();
	row->lazyInitialize(st::groupCallMembersListItem);

	// Mute.
	const auto &icon = st::groupCallVideoCrossLine.icon;
	const auto iconLeft = x + width - st.iconPosition.x() - icon.width();
	const auto iconTop = y + (height
		- st.iconPosition.y()
		- icon.height()
		+ shift);
	row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::Video);

	// Name.
	p.setPen(st::groupCallVideoTextFg);
	const auto hasWidth = width
		- st.iconPosition.x() - icon.width()
		- st.namePosition.x();
	const auto nameLeft = x + st.namePosition.x();
	row->name().drawLeftElided(
		p,
		nameLeft,
		nameTop + shift,
		hasWidth,
		width);
}

} // namespace Calls::Group
