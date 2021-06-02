/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_renderer.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_primitives.h"

namespace Media::View {

class OverlayWidget::RendererGL final : public OverlayWidget::Renderer {
public:
	RendererGL(not_null<OverlayWidget*> owner) : _owner(owner) {
	}

	void init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

	void deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

	void resize(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f,
		int w,
		int h);

	void paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

private:
	bool handleHideWorkaround(QOpenGLFunctions &f);
	void setDefaultViewport(QOpenGLFunctions &f);

	void paintBackground();
	void paintTransformedVideoFrame(QRect rect, int rotation) override;
	void paintTransformedStaticContent(
		const QImage &image,
		QRect rect,
		int rotation,
		bool fillTransparentBackground) override;
	void paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) override;
	void paintThemePreview(QRect outer) override;
	void paintDocumentBubble(QRect outer, QRect icon) override;
	void paintSaveMsg(QRect outer) override;
	void paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) override;
	void paintFooter(QRect outer, float64 opacity) override;
	void paintCaption(QRect outer, float64 opacity) override;
	void paintGroupThumbs(QRect outer, float64 opacity) override;

	void paintToCache(
		QImage &cache,
		QSize size,
		Fn<void(Painter&&)> method,
		bool clear = false);

	const not_null<OverlayWidget*> _owner;

	QOpenGLFunctions *_f = nullptr;
	Ui::GL::BackgroundFiller _background;
	QSize _viewport;
	float _factor = 1.;

	QImage _radialCache;
	QImage _documentBubbleCache;
	QImage _themePreviewCache;
	QImage _saveMsgCache;
	QImage _footerCache;
	QImage _captionCache;
	QImage _groupThumbsCache;

};

} // namespace Media::View
