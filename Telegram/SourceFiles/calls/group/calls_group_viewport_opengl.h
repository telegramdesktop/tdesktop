/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/gl/gl_surface.h"

#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLShaderProgram>

namespace Calls::Group {

class Viewport::RendererGL final : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<Viewport*> owner);

	void init(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

	void deinit(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

	void resize(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f,
		int w,
		int h) override;

	void paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

private:
	const not_null<Viewport*> _owner;

	std::optional<QOpenGLBuffer> _frameBuffer;
	std::optional<QOpenGLBuffer> _fillBuffer;
	std::optional<QOpenGLBuffer> _bgBuffer;
	std::optional<QOpenGLShaderProgram> _frameProgram;
	std::optional<QOpenGLShaderProgram> _fillProgram;
	std::optional<QOpenGLShaderProgram> _bgProgram;

};

} // namespace Calls::Group
