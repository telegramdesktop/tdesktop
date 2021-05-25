/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/round_rect.h"
#include "ui/effects/cross_line.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_image.h"

#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLShaderProgram>

namespace Calls::Group {

class Viewport::RendererGL final : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<Viewport*> owner);

	void free(const Textures &textures);

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
		int h) override;

	void paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

private:
	void fillBackground(QOpenGLFunctions &f);
	void paintTile(
		QOpenGLFunctions &f,
		not_null<VideoTile*> tile);
	void freeTextures(QOpenGLFunctions &f);
	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(const Ui::GL::Rect &raster) const;

	void ensureARGB32Program();
	void ensureButtonsImage();

	const not_null<Viewport*> _owner;

	GLfloat _factor = 1.;
	QSize _viewport;
	std::optional<QOpenGLBuffer> _frameBuffer;
	std::optional<QOpenGLBuffer> _bgBuffer;
	std::optional<QOpenGLShaderProgram> _argb32Program;
	std::optional<QOpenGLShaderProgram> _yuv420Program;
	std::optional<QOpenGLShaderProgram> _imageProgram;
	std::optional<QOpenGLShaderProgram> _bgProgram;
	QOpenGLShader *_frameVertexShader = nullptr;

	Ui::GL::Image _buttons;
	QRect _pinOn;
	QRect _pinOff;
	QRect _muteOn;
	QRect _muteOff;

	std::vector<GLfloat> _bgTriangles;
	std::vector<Textures> _texturesToFree;
	Ui::CrossLineAnimation _pinIcon;
	Ui::CrossLineAnimation _muteIcon;
	Ui::RoundRect _pinBackground;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
