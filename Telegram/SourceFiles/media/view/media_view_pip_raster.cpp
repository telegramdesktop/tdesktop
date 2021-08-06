/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip_raster.h"

#include "ui/image/image_prepare.h"
#include "ui/widgets/shadow.h"
#include "styles/style_calls.h" // st::callShadow.

namespace Media::View {
namespace {

[[nodiscard]] Streaming::FrameRequest UnrotateRequest(
		const Streaming::FrameRequest &request,
		int rotation) {
	if (!rotation) {
		return request;
	}
	const auto unrotatedCorner = [&](RectPart corner) {
		if (!(request.corners & corner)) {
			return RectPart(0);
		}
		switch (corner) {
		case RectPart::TopLeft:
			return (rotation == 90)
				? RectPart::BottomLeft
				: (rotation == 180)
				? RectPart::BottomRight
				: RectPart::TopRight;
		case RectPart::TopRight:
			return (rotation == 90)
				? RectPart::TopLeft
				: (rotation == 180)
				? RectPart::BottomLeft
				: RectPart::BottomRight;
		case RectPart::BottomRight:
			return (rotation == 90)
				? RectPart::TopRight
				: (rotation == 180)
				? RectPart::TopLeft
				: RectPart::BottomLeft;
		case RectPart::BottomLeft:
			return (rotation == 90)
				? RectPart::BottomRight
				: (rotation == 180)
				? RectPart::TopRight
				: RectPart::TopLeft;
		}
		Unexpected("Corner in rotateCorner.");
	};
	auto result = request;
	result.outer = FlipSizeByRotation(request.outer, rotation);
	result.resize = FlipSizeByRotation(request.resize, rotation);
	result.corners = unrotatedCorner(RectPart::TopLeft)
		| unrotatedCorner(RectPart::TopRight)
		| unrotatedCorner(RectPart::BottomRight)
		| unrotatedCorner(RectPart::BottomLeft);
	return result;
}

} // namespace

Pip::RendererSW::RendererSW(not_null<Pip*> owner)
: _owner(owner)
, _roundRect(ImageRoundRadius::Large, st::radialBg) {
}

void Pip::RendererSW::paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) {
	_p = &p;
	_clip = &clip;
	_clipOuter = clip.boundingRect();
	_owner->paint(this);
	_p = nullptr;
	_clip = nullptr;
}

void Pip::RendererSW::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	paintTransformedImage(
		_owner->videoFrame(frameRequest(geometry)),
		geometry);
}

void Pip::RendererSW::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) {
	paintTransformedImage(
		staticContentByRequest(image, frameRequest(geometry)),
		geometry);
}

void Pip::RendererSW::paintFade(ContentGeometry geometry) const {
	using Part = RectPart;
	const auto sides = geometry.attached;
	const auto rounded = RectPart(0)
		| ((sides & (Part::Top | Part::Left)) ? Part(0) : Part::TopLeft)
		| ((sides & (Part::Top | Part::Right)) ? Part(0) : Part::TopRight)
		| ((sides & (Part::Bottom | Part::Right))
			? Part(0)
			: Part::BottomRight)
		| ((sides & (Part::Bottom | Part::Left))
			? Part(0)
			: Part::BottomLeft);
	_roundRect.paintSomeRounded(
		*_p,
		geometry.inner,
		rounded | Part::NoTopBottom | Part::Top | Part::Bottom);
}

void Pip::RendererSW::paintButtonsStart() {
}

void Pip::RendererSW::paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) {
	if (over < 1.) {
		_p->setOpacity(shown);
		icon.paint(*_p, button.icon.x(), button.icon.y(), outerWidth);
	}
	if (over > 0.) {
		_p->setOpacity(over * shown);
		iconOver.paint(*_p, button.icon.x(), button.icon.y(), outerWidth);
	}
}

Pip::FrameRequest Pip::RendererSW::frameRequest(
		ContentGeometry geometry) const {
	auto result = FrameRequest();
	result.outer = geometry.inner.size() * style::DevicePixelRatio();
	result.resize = result.outer;
	result.corners = RectPart(0)
		| ((geometry.attached & (RectPart::Left | RectPart::Top))
			? RectPart(0)
			: RectPart::TopLeft)
		| ((geometry.attached & (RectPart::Top | RectPart::Right))
			? RectPart(0)
			: RectPart::TopRight)
		| ((geometry.attached & (RectPart::Right | RectPart::Bottom))
			? RectPart(0)
			: RectPart::BottomRight)
		| ((geometry.attached & (RectPart::Bottom | RectPart::Left))
			? RectPart(0)
			: RectPart::BottomLeft);
	result.radius = ImageRoundRadius::Large;
	return UnrotateRequest(result, geometry.rotation);
}

QImage Pip::RendererSW::staticContentByRequest(
		const QImage &image,
		const FrameRequest &request) {
	if (request.resize.isEmpty()) {
		return QImage();
	} else if (!_preparedStaticContent.isNull()
		&& _preparedStaticRequest == request
		&& image.cacheKey() == _preparedStaticKey) {
		return _preparedStaticContent;
	}
	_preparedStaticKey = image.cacheKey();
	_preparedStaticRequest = request;
	//_preparedCoverStorage = Streaming::PrepareByRequest(
	//	_instance.info().video.cover,
	//	false,
	//	_instance.info().video.rotation,
	//	request,
	//	std::move(_preparedCoverStorage));
	using Option = Images::Option;
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| ((request.corners & RectPart::TopLeft)
			? Option::RoundedTopLeft
			: Option(0))
		| ((request.corners & RectPart::TopRight)
			? Option::RoundedTopRight
			: Option(0))
		| ((request.corners & RectPart::BottomRight)
			? Option::RoundedBottomRight
			: Option(0))
		| ((request.corners & RectPart::BottomLeft)
			? Option::RoundedBottomLeft
			: Option(0));
	_preparedStaticContent = Images::prepare(
		image,
		request.resize.width(),
		request.resize.height(),
		options,
		request.outer.width(),
		request.outer.height());
	return _preparedStaticContent;
}

void Pip::RendererSW::paintTransformedImage(
		const QImage &image,
		ContentGeometry geometry) {
	const auto rect = geometry.inner;
	const auto rotation = geometry.rotation;
	if (geometry.useTransparency) {
		Ui::Shadow::paint(*_p, rect, geometry.outer.width(), st::callShadow);
	}

	if (UsePainterRotation(rotation)) {
		if (rotation) {
			_p->save();
			_p->rotate(rotation);
		}
		PainterHighQualityEnabler hq(*_p);
		_p->drawImage(RotatedRect(rect, rotation), image);
		if (rotation) {
			_p->restore();
		}
	} else {
		_p->drawImage(rect, RotateFrameImage(image, rotation));
	}

	if (geometry.fade > 0) {
		_p->setOpacity(geometry.fade);
		paintFade(geometry);
	}
}

void Pip::RendererSW::paintRadialLoading(
		QRect inner,
		float64 controlsShown) {
	_owner->paintRadialLoadingContent(*_p, inner, st::radialFg->c);
}

void Pip::RendererSW::paintPlayback(QRect outer, float64 shown) {
	_owner->paintPlaybackContent(*_p, outer, shown);
}

void Pip::RendererSW::paintVolumeController(QRect outer, float64 shown) {
	_owner->paintVolumeControllerContent(*_p, outer, shown);
}

} // namespace Media::View
