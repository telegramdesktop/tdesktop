/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip_raster.h"

#include "ui/image/image_prepare.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "styles/style_calls.h" // st::callShadow.

namespace Media::View {
namespace {

[[nodiscard]] Streaming::FrameRequest UnrotateRequest(
		const Streaming::FrameRequest &request,
		int rotation) {
	if (!rotation) {
		return request;
	}
	const auto unrotatedCorner = [&](int index) {
		using namespace Images;
		switch (index) {
		case kTopLeft:
			return (rotation == 90)
				? kBottomLeft
				: (rotation == 180)
				? kBottomRight
				: kTopRight;
		case kTopRight:
			return (rotation == 90)
				? kTopLeft
				: (rotation == 180)
				? kBottomLeft
				: kBottomRight;
		case kBottomRight:
			return (rotation == 90)
				? kTopRight
				: (rotation == 180)
				? kTopLeft
				: kBottomLeft;
		case kBottomLeft:
			return (rotation == 90)
				? kBottomRight
				: (rotation == 180)
				? kTopRight
				: kTopLeft;
		}
		Unexpected("Corner in rotateCorner.");
	};
	auto result = request;
	result.outer = FlipSizeByRotation(request.outer, rotation);
	result.resize = FlipSizeByRotation(request.resize, rotation);
	auto rounding = result.rounding;
	for (auto i = 0; i != 4; ++i) {
		result.rounding.p[unrotatedCorner(i)] = rounding.p[i];
	}
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
	using namespace Images;
	auto result = FrameRequest();
	result.outer = geometry.inner.size() * style::DevicePixelRatio();
	result.resize = result.outer;
	result.rounding = CornersMaskRef(CornersMask(ImageRoundRadius::Large));
	if (geometry.attached & (RectPart::Top | RectPart::Left)) {
		result.rounding.p[kTopLeft] = nullptr;
	}
	if (geometry.attached & (RectPart::Top | RectPart::Right)) {
		result.rounding.p[kTopRight] = nullptr;
	}
	if (geometry.attached & (RectPart::Bottom | RectPart::Left)) {
		result.rounding.p[kBottomLeft] = nullptr;
	}
	if (geometry.attached & (RectPart::Bottom | RectPart::Right)) {
		result.rounding.p[kBottomRight] = nullptr;
	}
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
	_preparedStaticContent = Images::Round(
		Images::Prepare(
			image,
			request.resize,
			{ .outer = request.outer / style::DevicePixelRatio() }),
		request.rounding);

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
