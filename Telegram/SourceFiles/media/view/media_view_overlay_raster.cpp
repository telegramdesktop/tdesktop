/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_raster.h"

#include "ui/painter.h"
#include "media/view/media_view_pip.h"
#include "platform/platform_overlay_widget.h"
#include "styles/style_media_view.h"

namespace Media::View {

OverlayWidget::RendererSW::RendererSW(not_null<OverlayWidget*> owner)
: _owner(owner)
, _transparentBrush(style::TransparentPlaceholder()) {
}

void OverlayWidget::RendererSW::paintFallback(
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

void OverlayWidget::RendererSW::paintBackground() {
	const auto region = _owner->opaqueContentShown()
		? (*_clip - _owner->finalContentRect())
		: *_clip;

	const auto m = _p->compositionMode();
	_p->setCompositionMode(QPainter::CompositionMode_Source);
	const auto &bg = _owner->_fullScreenVideo
		? st::mediaviewVideoBg
		: st::mediaviewBg;
	for (const auto &rect : region) {
		_p->fillRect(rect, bg);
	}
	_p->setCompositionMode(m);
}

QRect OverlayWidget::RendererSW::TransformRect(
		QRectF geometry,
		int rotation) {
	const auto center = geometry.center();
	const auto rect = ((rotation % 180) == 90)
		? QRectF(
			center.x() - geometry.height() / 2.,
			center.y() - geometry.width() / 2.,
			geometry.height(),
			geometry.width())
		: geometry;
	return QRect(
		int(rect.x()),
		int(rect.y()),
		int(rect.width()),
		int(rect.height()));
}

void OverlayWidget::RendererSW::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	Expects(_owner->_streamed != nullptr);

	const auto rotation = int(geometry.rotation);
	const auto rect = TransformRect(geometry.rect, rotation);
	if (!rect.intersects(_clipOuter)) {
		return;
	}
	paintTransformedImage(_owner->videoFrame(), rect, rotation);
	paintControlsFade(rect, geometry.controlsOpacity);
}

void OverlayWidget::RendererSW::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground) {
	const auto rotation = int(geometry.rotation);
	const auto rect = TransformRect(geometry.rect, rotation);
	if (!rect.intersects(_clipOuter)) {
		return;
	}

	if (fillTransparentBackground) {
		_p->fillRect(rect, _transparentBrush);
	}
	if (!image.isNull()) {
		paintTransformedImage(image, rect, rotation);
	}
	paintControlsFade(rect, geometry.controlsOpacity);
}

void OverlayWidget::RendererSW::paintControlsFade(
		QRect geometry,
		float64 opacity) {
	_p->setOpacity(opacity);
	_p->setClipRect(geometry);
	const auto width = _owner->width();
	const auto &top = st::mediaviewShadowTop;
	const auto topShadow = QRect(
		QPoint(width - top.width(), 0),
		top.size());
	if (topShadow.intersected(geometry).intersects(_clipOuter)) {
		top.paint(*_p, topShadow.topLeft(), width);
	}
	const auto &bottom = st::mediaviewShadowBottom;
	const auto bottomShadow = QRect(
		QPoint(0, _owner->height() - bottom.height()),
		QSize(width, bottom.height()));
	if (bottomShadow.intersected(geometry).intersects(_clipOuter)) {
		bottom.fill(*_p, bottomShadow);
	}
	_p->setClipping(false);
	_p->setOpacity(1.);
}

void OverlayWidget::RendererSW::paintTransformedImage(
		const QImage &image,
		QRect rect,
		int rotation) {
	PainterHighQualityEnabler hq(*_p);
	if (UsePainterRotation(rotation)) {
		if (rotation) {
			_p->save();
			_p->rotate(rotation);
		}
		_p->drawImage(RotatedRect(rect, rotation), image);
		if (rotation) {
			_p->restore();
		}
	} else {
		_p->drawImage(rect, _owner->transformShownContent(image, rotation));
	}
}

void OverlayWidget::RendererSW::paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) {
	_owner->paintRadialLoadingContent(*_p, inner, radial, radialOpacity);
}

void OverlayWidget::RendererSW::paintThemePreview(QRect outer) {
	_owner->paintThemePreviewContent(*_p, outer, _clipOuter);
}

void OverlayWidget::RendererSW::paintDocumentBubble(
		QRect outer,
		QRect icon) {
	if (outer.intersects(_clipOuter)) {
		_owner->paintDocumentBubbleContent(*_p, outer, icon, _clipOuter);
		if (icon.intersects(_clipOuter)) {
			_owner->paintRadialLoading(this);
		}
	}
}

void OverlayWidget::RendererSW::paintSaveMsg(QRect outer) {
	if (outer.intersects(_clipOuter)) {
		_owner->paintSaveMsgContent(*_p, outer, _clipOuter);
	}
}

void OverlayWidget::RendererSW::paintControlsStart() {
}

void OverlayWidget::RendererSW::paintControl(
		OverState control,
		QRect over,
		float64 overOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	if (!over.isEmpty() && !over.intersects(_clipOuter)) {
		return;
	}
	if (!over.isEmpty() && overOpacity > 0) {
		if (_overControlImage.isNull()) {
			validateOverControlImage();
		}
		_p->setOpacity(overOpacity);
		_p->drawImage(over.topLeft(), _overControlImage);
	}
	if (inner.intersects(_clipOuter)) {
		_p->setOpacity(innerOpacity);
		icon.paintInCenter(*_p, inner);
	}
}

void OverlayWidget::RendererSW::paintFooter(QRect outer, float64 opacity) {
	if (outer.intersects(_clipOuter)) {
		_owner->paintFooterContent(*_p, outer, _clipOuter, opacity);
	}
}

void OverlayWidget::RendererSW::paintCaption(QRect outer, float64 opacity) {
	if (outer.intersects(_clipOuter)) {
		_owner->paintCaptionContent(*_p, outer, _clipOuter, opacity);
	}
}

void OverlayWidget::RendererSW::paintGroupThumbs(
		QRect outer,
		float64 opacity) {
	if (outer.intersects(_clipOuter)) {
		_owner->paintGroupThumbsContent(*_p, outer, _clipOuter, opacity);
	}
}

void OverlayWidget::RendererSW::paintRoundedCorners(int radius) {
	// The RpWindow rounding overlay will do the job.
}

void OverlayWidget::RendererSW::validateOverControlImage() {
	const auto size = QSize(st::mediaviewIconOver, st::mediaviewIconOver);
	const auto alpha = base::SafeRound(kOverBackgroundOpacity * 255);
	_overControlImage = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_overControlImage.setDevicePixelRatio(style::DevicePixelRatio());
	_overControlImage.fill(Qt::transparent);

	Painter p(&_overControlImage);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	auto color = OverBackgroundColor();
	color.setAlpha(alpha);
	p.setBrush(color);
	p.drawEllipse(QRect(QPoint(), size));
}

} // namespace Media::View
