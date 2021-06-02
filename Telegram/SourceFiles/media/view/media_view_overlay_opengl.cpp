/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_opengl.h"

#include "base/platform/base_platform_info.h"

namespace Media::View {

void OverlayWidget::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_background.init(f);
}

void OverlayWidget::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_background.deinit(f);
}

void OverlayWidget::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f,
		int w,
		int h) {
	_factor = widget->devicePixelRatio();
	_viewport = QSize(w, h);
	setDefaultViewport(f);
}

void OverlayWidget::RendererGL::setDefaultViewport(QOpenGLFunctions &f) {
	const auto size = _viewport * _factor;
	f.glViewport(0, 0, size.width(), size.height());
}

void OverlayWidget::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	if (handleHideWorkaround(f)) {
		return;
	}
	_f = &f;
	_owner->paint(this);
}

bool OverlayWidget::RendererGL::handleHideWorkaround(QOpenGLFunctions &f) {
	if (!Platform::IsWindows() || !_owner->_hideWorkaround) {
		return false;
	}
	// This is needed on Windows,
	// because on reopen it blinks with the last shown content.
	f.glClearColor(0., 0., 0., 0.);
	f.glClear(GL_COLOR_BUFFER_BIT);
	return true;
}

void OverlayWidget::RendererGL::paintBackground() {
	const auto &bg = _owner->_fullScreenVideo
		? st::mediaviewVideoBg
		: st::mediaviewBg;
	auto fill = QRegion(QRect(QPoint(), _viewport));
	if (_owner->opaqueContentShown()) {
		fill -= _owner->contentRect();
	}
	_background.fill(
		*_f,
		fill,
		_viewport,
		_factor,
		bg);
}

void OverlayWidget::RendererGL::paintTransformedVideoFrame(
		QRect rect,
		int rotation) {
}

void OverlayWidget::RendererGL::paintTransformedStaticContent(
		const QImage &image,
		QRect rect,
		int rotation,
		bool fillTransparentBackground) {
}

void OverlayWidget::RendererGL::paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) {
	paintToCache(_radialCache, inner.size(), [&](Painter &&p) {
		const auto newInner = QRect(QPoint(), inner.size());
		_owner->paintRadialLoadingContent(p, newInner, radial, radialOpacity);
	}, true);
	//p.drawImage(inner.topLeft(), _radialCache);
}

void OverlayWidget::RendererGL::paintThemePreview(QRect outer) {
	paintToCache(_themePreviewCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintThemePreviewContent(p, newOuter, newOuter);
	});
}

void OverlayWidget::RendererGL::paintDocumentBubble(
		QRect outer,
		QRect icon) {
	paintToCache(_documentBubbleCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		const auto newIcon = icon.translated(-outer.topLeft());
		_owner->paintDocumentBubbleContent(p, newOuter, newIcon, newOuter);
	});
	//p.drawImage(outer.topLeft(), _documentBubbleCache);
	_owner->paintRadialLoading(this);
}

void OverlayWidget::RendererGL::paintSaveMsg(QRect outer) {
	paintToCache(_saveMsgCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSaveMsgContent(p, newOuter, newOuter);
	}, true);
	//p.drawImage(outer.topLeft(), _saveMsgCache);
}

void OverlayWidget::RendererGL::paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {

}

void OverlayWidget::RendererGL::paintFooter(QRect outer, float64 opacity) {
	paintToCache(_footerCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintFooterContent(p, newOuter, newOuter, opacity);
	}, true);
	//p.drawImage(outer, _footerCache, QRect(QPoint(), outer.size()) * factor);
}

void OverlayWidget::RendererGL::paintCaption(QRect outer, float64 opacity) {
	paintToCache(_captionCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintCaptionContent(p, newOuter, newOuter, opacity);
	});
	//p.drawImage(outer, _captionCache, ...);
}

void OverlayWidget::RendererGL::paintGroupThumbs(
		QRect outer,
		float64 opacity) {
	paintToCache(_groupThumbsCache, outer.size(), [&](Painter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintGroupThumbsContent(p, newOuter, newOuter, opacity);
	});
}

void OverlayWidget::RendererGL::paintToCache(
		QImage &cache,
		QSize size,
		Fn<void(Painter&&)> method,
		bool clear) {
	if (cache.width() < size.width() * _factor
		|| cache.height() < size.height() * _factor) {
		cache = QImage(
			size * _factor,
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(_factor);
	} else if (cache.devicePixelRatio() != _factor) {
		cache.setDevicePixelRatio(_factor);
	}
	if (clear) {
		cache.fill(Qt::transparent);
	}
	method(Painter(&cache));
}

} // namespace Media::View
