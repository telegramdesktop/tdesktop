/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_pip_renderer.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_primitives.h"

#include <QtGui/QOpenGLBuffer>

namespace Media::View {

class Pip::RendererGL final : public Pip::Renderer {
public:
	explicit RendererGL(not_null<Pip*> owner);

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
		not_null<const style::icon*> iconOver;
	};
	void createShadowTexture();

	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) override;
	void paintTransformedContent(
		not_null<QOpenGLShaderProgram*> program,
		ContentGeometry geometry);
	void paintRadialLoading(
		QRect inner,
		float64 controlsShown) override;
	void paintButtonsStart() override;
	void paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) override;
	void paintPlayback(QRect outer, float64 shown) override;
	void paintVolumeController(QRect outer, float64 shown) override;

	void paintUsingRaster(
		Ui::GL::Image &image,
		QRect rect,
		Fn<void(Painter&&)> method,
		int bufferOffset,
		bool transparent = false);

	void validateControls();
	void invalidateControls();
	void toggleBlending(bool enabled);

	[[nodiscard]] QRect RoundingRect(ContentGeometry geometry);

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

	const not_null<Pip*> _owner;

	QOpenGLFunctions *_f = nullptr;
	QSize _viewport;
	float _factor = 1.;
	QVector2D _uniformViewport;

	std::optional<QOpenGLBuffer> _contentBuffer;
	std::optional<QOpenGLShaderProgram> _imageProgram;
	std::optional<QOpenGLShaderProgram> _controlsProgram;
	QOpenGLShader *_texturedVertexShader = nullptr;
	std::optional<QOpenGLShaderProgram> _argb32Program;
	std::optional<QOpenGLShaderProgram> _yuv420Program;
	Ui::GL::Textures<4> _textures;
	QSize _rgbaSize;
	QSize _lumaSize;
	QSize _chromaSize;
	quint64 _cacheKey = 0;
	int _trackFrameIndex = 0;

	Ui::GL::Image _radialImage;
	Ui::GL::Image _controlsImage;
	Ui::GL::Image _playbackImage;
	Ui::GL::Image _volumeControllerImage;
	Ui::GL::Image _shadowImage;

	static constexpr auto kControlsCount = 7;
	[[nodiscard]] static Control ControlMeta(
		OverState control,
		int index = 0);
	std::array<QRect, kControlsCount * 2> _controlsTextures;

	bool _blendingEnabled = false;

	rpl::lifetime _lifetime;

};

} // namespace Media::View
