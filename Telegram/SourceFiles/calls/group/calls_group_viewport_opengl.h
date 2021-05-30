/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/round_rect.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_image.h"

#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLShaderProgram>

namespace Webrtc {
struct FrameWithInfo;
} // namespace Webrtc

namespace Calls::Group {

class Viewport::RendererGL final : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<Viewport*> owner);

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
	struct TileData {
		quintptr id = 0;
		not_null<PeerData*> peer;
		Ui::GL::Textures<5> textures;
		Ui::GL::Framebuffers<2> framebuffers;
		Ui::Animations::Simple outlined;
		QRect nameRect;
		int nameVersion = 0;
		mutable int textureIndex = 0;
		mutable int trackIndex = -1;
		mutable QSize rgbaSize;
		mutable QSize textureSize;
		mutable QSize textureChromaSize;
		mutable QSize textureBlurSize;
		bool stale = false;
		bool outline = false;
	};
	struct Program {
		std::optional<QOpenGLShaderProgram> argb32;
		std::optional<QOpenGLShaderProgram> yuv420;
	};

	void setDefaultViewport(QOpenGLFunctions &f);
	void fillBackground(QOpenGLFunctions &f);
	void paintTile(
		QOpenGLFunctions &f,
		GLuint defaultFramebufferObject,
		not_null<VideoTile*> tile,
		TileData &nameData);
	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(
		const Ui::GL::Rect &raster) const;

	void ensureARGB32Program();
	void ensureButtonsImage();
	void prepareObjects(
		QOpenGLFunctions &f,
		TileData &tileData,
		QSize blurSize);
	void bindFrame(
		QOpenGLFunctions &f,
		const Webrtc::FrameWithInfo &data,
		TileData &tileData,
		Program &program);
	void drawDownscalePass(
		QOpenGLFunctions &f,
		TileData &tileData);
	void drawFirstBlurPass(
		QOpenGLFunctions &f,
		TileData &tileData,
		QSize blurSize);
	void validateDatas();
	void validateOutlineAnimation(
		not_null<VideoTile*> tile,
		TileData &data);

	const not_null<Viewport*> _owner;

	GLfloat _factor = 1.;
	QSize _viewport;
	bool _rgbaFrame = false;
	std::optional<QOpenGLBuffer> _frameBuffer;
	std::optional<QOpenGLBuffer> _bgBuffer;
	Program _downscaleProgram;
	std::optional<QOpenGLShaderProgram> _blurProgram;
	Program _frameProgram;
	std::optional<QOpenGLShaderProgram> _imageProgram;
	std::optional<QOpenGLShaderProgram> _bgProgram;
	QOpenGLShader *_downscaleVertexShader = nullptr;
	QOpenGLShader *_frameVertexShader = nullptr;

	Ui::GL::Image _buttons;
	QRect _pinOn;
	QRect _pinOff;
	QRect _back;
	QRect _muteOn;
	QRect _muteOff;

	Ui::GL::Image _names;
	std::vector<TileData> _tileData;
	std::vector<int> _tileDataIndices;

	std::vector<GLfloat> _bgTriangles;
	Ui::CrossLineAnimation _pinIcon;
	Ui::CrossLineAnimation _muteIcon;
	Ui::RoundRect _pinBackground;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
