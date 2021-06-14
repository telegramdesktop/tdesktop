/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_renderer.h"
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

	void paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

	std::optional<QColor> clearColor() override;

private:
	struct Control {
		int index = -1;
		not_null<const style::icon*> icon;
	};
	bool handleHideWorkaround(QOpenGLFunctions &f);

	void paintBackground() override;
	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground) override;
	void paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		ContentGeometry geometry);
	void paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) override;
	void paintThemePreview(QRect outer) override;
	void paintDocumentBubble(QRect outer, QRect icon) override;
	void paintSaveMsg(QRect outer) override;
	void paintControlsStart() override;
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

	void invalidate();

	void paintUsingRaster(
		Ui::GL::Image &image,
		QRect rect,
		Fn<void(Painter&&)> method,
		int bufferOffset,
		bool transparent = false);

	void validateControls();
	void invalidateControls();
	void toggleBlending(bool enabled);

	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(const QRectF &raster) const;
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
	QSize _viewport;
	float _factor = 1.;
	QVector2D _uniformViewport;

	std::optional<QOpenGLBuffer> _contentBuffer;
	std::optional<QOpenGLShaderProgram> _imageProgram;
	QOpenGLShader *_texturedVertexShader = nullptr;
	std::optional<QOpenGLShaderProgram> _withTransparencyProgram;
	std::optional<QOpenGLShaderProgram> _yuv420Program;
	std::optional<QOpenGLShaderProgram> _fillProgram;
	std::optional<QOpenGLShaderProgram> _controlsProgram;
	Ui::GL::Textures<4> _textures;
	QSize _rgbaSize;
	QSize _lumaSize;
	QSize _chromaSize;
	qint64 _cacheKey = 0;
	int _trackFrameIndex = 0;
	int _streamedIndex = 0;

	Ui::GL::Image _radialImage;
	Ui::GL::Image _documentBubbleImage;
	Ui::GL::Image _themePreviewImage;
	Ui::GL::Image _saveMsgImage;
	Ui::GL::Image _footerImage;
	Ui::GL::Image _captionImage;
	Ui::GL::Image _groupThumbsImage;
	Ui::GL::Image _controlsImage;

	static constexpr auto kControlsCount = 6;
	[[nodiscard]] static Control ControlMeta(OverState control);
	std::array<QRect, kControlsCount> _controlsTextures;

	bool _blendingEnabled = false;

	rpl::lifetime _lifetime;

};

} // namespace Media::View
