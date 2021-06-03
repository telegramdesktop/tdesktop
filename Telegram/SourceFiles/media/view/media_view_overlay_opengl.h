/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_renderer.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_primitives.h"

#include <QtGui/QOpenGLBuffer>

namespace Media::View {

class OverlayWidget::RendererGL final : public OverlayWidget::Renderer {
public:
	explicit RendererGL(not_null<OverlayWidget*> owner);

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

	void paintUsingRaster(
		Ui::GL::Image &image,
		QRect rect,
		Fn<void(Painter&&)> method,
		int bufferOffset,
		bool transparent = false);

	void toggleBlending(bool enabled);

	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(
		const Ui::GL::Rect &raster) const;

	void uploadTexture(
		GLint internalformat,
		GLint format,
		QSize size,
		QSize hasSize,
		int stride,
		const void *data) const;

	const not_null<OverlayWidget*> _owner;

	QOpenGLFunctions *_f = nullptr;
	Ui::GL::BackgroundFiller _background;
	QSize _viewport;
	float _factor = 1.;

	std::optional<QOpenGLBuffer> _contentBuffer;
	std::optional<QOpenGLShaderProgram> _imageProgram;

	Ui::GL::Textures<3> _textures;
	QSize _rgbaSize;
	QSize _ySize;
	quint64 _cacheKey = 0;

	Ui::GL::Image _radialImage;
	Ui::GL::Image _documentBubbleImage;
	Ui::GL::Image _themePreviewImage;
	Ui::GL::Image _saveMsgImage;
	Ui::GL::Image _footerImage;
	Ui::GL::Image _captionImage;
	Ui::GL::Image _groupThumbsImage;
	bool _blendingEnabled = false;

	rpl::lifetime _lifetime;

};

} // namespace Media::View
