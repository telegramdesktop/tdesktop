/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_raster.h"

#include "ui/painter.h"
#include "media/stories/media_stories_view.h"
#include "media/view/media_view_pip.h"
#include "platform/platform_overlay_widget.h"
#include "styles/style_media_view.h"

namespace Media::View {

OverlayWidget::RendererSW::RendererSW(not_null<OverlayWidget*> owner)
: _owner(owner)
, _transparentBrush(style::TransparentPlaceholder()) {
}

bool OverlayWidget::RendererSW::handleHideWorkaround() {
	// This is needed on Windows or Linux,
	// because on reopen it blinks with the last shown content.
	return _owner->_hideWorkaround != nullptr;
}

void OverlayWidget::RendererSW::paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) {
	if (handleHideWorkaround()) {
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(clip.boundingRect(), Qt::transparent);
		return;
	}
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
	if (const auto notch = _owner->topNotchSkip()) {
		const auto top = QRect(0, 0, _owner->width(), notch);
		if (const auto black = top.intersected(_clipOuter); !black.isEmpty()) {
			_p->fillRect(black, Qt::black);
		}
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
	paintControlsFade(rect, geometry);
}

void OverlayWidget::RendererSW::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground,
		int index) {
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
	paintControlsFade(rect, geometry);
}

void OverlayWidget::RendererSW::paintControlsFade(
		QRect content,
		const ContentGeometry &geometry) {
	auto opacity = geometry.controlsOpacity;
	if (geometry.fade > 0.) {
		_p->setOpacity(geometry.fade);
		_p->fillRect(content, Qt::black);
		opacity *= 1. - geometry.fade;
	}

	_p->setOpacity(opacity);
	_p->setClipRect(content);
	const auto width = _owner->width();
	const auto stories = (_owner->_stories != nullptr);
	if (!stories || geometry.topShadowShown) {
		const auto flip = !stories && !_owner->topShadowOnTheRight();
		const auto &top = stories
			? st::storiesShadowTop
			: st::mediaviewShadowTop;
		const auto topShadow = stories
			? QRect(
				content.topLeft(),
				QSize(content.width(), top.height()))
			: QRect(
				QPoint(flip ? 0 : (width - top.width()), 0),
				top.size());
		if (topShadow.intersected(content).intersects(_clipOuter)) {
			if (stories) {
				top.fill(*_p, topShadow);
			} else if (flip) {
				if (_topShadowCache.isNull()
					|| _topShadowColor != st::windowShadowFg->c) {
					_topShadowColor = st::windowShadowFg->c;
					_topShadowCache = top.instance(
						_topShadowColor).mirrored(true, false);
				}
				_p->drawImage(0, 0, _topShadowCache);
			} else {
				top.paint(*_p, topShadow.topLeft(), width);
			}
		}
	}
	const auto &bottom = stories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	const auto bottomStart = _owner->height() - geometry.bottomShadowSkip;
	const auto bottomShadow = QRect(
		QPoint(0, bottomStart - bottom.height()),
		QSize(width, bottom.height()));
	if (bottomShadow.intersected(content).intersects(_clipOuter)) {
		bottom.fill(*_p, bottomShadow);
	}
	_p->setClipping(false);
	_p->setOpacity(1.);
	if (bottomStart < content.y() + content.height()) {
		_p->fillRect(
			content.x(),
			bottomStart,
			content.width(),
			content.y() + content.height() - bottomStart,
			QColor(0, 0, 0, 88));
	}
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
		Over control,
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

void OverlayWidget::RendererSW::paintStoriesSiblingPart(
		int index,
		const QImage &image,
		QRect rect,
		float64 opacity) {
	const auto changeOpacity = (opacity != 1.);
	if (changeOpacity) {
		_p->setOpacity(opacity);
	}
	_p->drawImage(rect, image);
	if (changeOpacity) {
		_p->setOpacity(1.);
	}
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
