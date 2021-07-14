/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_video_incoming.h"

#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_shader.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_primitives.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"

#include <QtGui/QOpenGLShader>
#include <QtGui/QOpenGLBuffer>

namespace Calls {
namespace {

constexpr auto kBottomShadowAlphaMax = 74;

using namespace Ui::GL;

[[nodiscard]] ShaderPart FragmentBottomShadow() {
	return {
		.header = R"(
uniform vec3 shadow; // fullHeight, shadowTop, maxOpacity
)",
		.body = R"(
	float shadowCoord = shadow.y - gl_FragCoord.y;
	float shadowValue = clamp(shadowCoord / shadow.x, 0., 1.);
	float shadowShown = shadowValue * shadow.z;
	result = vec4(min(result.rgb, vec3(1.)) * (1. - shadowShown), result.a);
)",
	};
}

} // namespace

class Panel::Incoming::RendererGL final : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<Incoming*> owner);

	void init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

	void deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

	void paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) override;

private:
	void uploadTexture(
		QOpenGLFunctions &f,
		GLint internalformat,
		GLint format,
		QSize size,
		QSize hasSize,
		int stride,
		const void *data) const;
	void validateShadowImage();

	const not_null<Incoming*> _owner;

	QSize _viewport;
	float _factor = 1.;
	QVector2D _uniformViewport;

	std::optional<QOpenGLBuffer> _contentBuffer;
	std::optional<QOpenGLShaderProgram> _argb32Program;
	QOpenGLShader *_texturedVertexShader = nullptr;
	std::optional<QOpenGLShaderProgram> _yuv420Program;
	std::optional<QOpenGLShaderProgram> _imageProgram;
	Ui::GL::Textures<4> _textures;
	QSize _rgbaSize;
	QSize _lumaSize;
	QSize _chromaSize;
	int _trackFrameIndex = 0;

	Ui::GL::Image _controlsShadowImage;
	QRect _controlsShadowLeft;
	QRect _controlsShadowRight;

	rpl::lifetime _lifetime;

};

class Panel::Incoming::RendererSW final : public Ui::GL::Renderer {
public:
	explicit RendererSW(not_null<Incoming*> owner);

	void paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) override;

private:
	void initBottomShadow();
	void fillTopShadow(QPainter &p);
	void fillBottomShadow(QPainter &p);

	const not_null<Incoming*> _owner;

	QImage _bottomShadow;

};

Panel::Incoming::RendererGL::RendererGL(not_null<Incoming*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_controlsShadowImage.invalidate();
	}, _lifetime);
}

void Panel::Incoming::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	constexpr auto kQuads = 2;
	constexpr auto kQuadVertices = kQuads * 4;
	constexpr auto kQuadValues = kQuadVertices * 4;

	_contentBuffer.emplace();
	_contentBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_contentBuffer->create();
	_contentBuffer->bind();
	_contentBuffer->allocate(kQuadValues * sizeof(GLfloat));

	_textures.ensureCreated(f);

	_imageProgram.emplace();
	_texturedVertexShader = LinkProgram(
		&*_imageProgram,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleARGB32Texture(),
		})).vertex;

	_argb32Program.emplace();
	LinkProgram(
		&*_argb32Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentBottomShadow(),
		}));

	_yuv420Program.emplace();
	LinkProgram(
		&*_yuv420Program,
		_texturedVertexShader,
		FragmentShader({
			FragmentSampleYUV420Texture(),
			FragmentBottomShadow(),
		}));
}

void Panel::Incoming::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_textures.destroy(f);
	_imageProgram = std::nullopt;
	_texturedVertexShader = nullptr;
	_argb32Program = std::nullopt;
	_yuv420Program = std::nullopt;
	_contentBuffer = std::nullopt;
}

void Panel::Incoming::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	const auto markGuard = gsl::finally([&] {
		_owner->_track->markFrameShown();
	});
	const auto data = _owner->_track->frameWithInfo(false);
	if (data.format == Webrtc::FrameFormat::None) {
		return;
	}

	const auto factor = widget->devicePixelRatio();
	if (_factor != factor) {
		_factor = factor;
		_controlsShadowImage.invalidate();
	}
	_viewport = widget->size();
	_uniformViewport = QVector2D(
		_viewport.width() * _factor,
		_viewport.height() * _factor);

	const auto rgbaFrame = (data.format == Webrtc::FrameFormat::ARGB32);
	const auto upload = (_trackFrameIndex != data.index);
	_trackFrameIndex = data.index;
	auto &program = rgbaFrame ? _argb32Program : _yuv420Program;
	program->bind();
	if (rgbaFrame) {
		Assert(!data.original.isNull());
		f.glActiveTexture(GL_TEXTURE0);
		_textures.bind(f, 0);
		if (upload) {
			uploadTexture(
				f,
				Ui::GL::kFormatRGBA,
				Ui::GL::kFormatRGBA,
				data.original.size(),
				_rgbaSize,
				data.original.bytesPerLine() / 4,
				data.original.constBits());
			_rgbaSize = data.original.size();
		}
		program->setUniformValue("s_texture", GLint(0));
	} else {
		Assert(data.format == Webrtc::FrameFormat::YUV420);
		Assert(!data.yuv420->size.isEmpty());
		const auto yuv = data.yuv420;
		const auto format = Ui::GL::CurrentSingleComponentFormat();

		f.glActiveTexture(GL_TEXTURE0);
		_textures.bind(f, 1);
		if (upload) {
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			uploadTexture(
				f,
				format,
				format,
				yuv->size,
				_lumaSize,
				yuv->y.stride,
				yuv->y.data);
			_lumaSize = yuv->size;
		}
		f.glActiveTexture(GL_TEXTURE1);
		_textures.bind(f, 2);
		if (upload) {
			uploadTexture(
				f,
				format,
				format,
				yuv->chromaSize,
				_chromaSize,
				yuv->u.stride,
				yuv->u.data);
		}
		f.glActiveTexture(GL_TEXTURE2);
		_textures.bind(f, 3);
		if (upload) {
			uploadTexture(
				f,
				format,
				format,
				yuv->chromaSize,
				_chromaSize,
				yuv->v.stride,
				yuv->v.data);
			_chromaSize = yuv->chromaSize;
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		}
		program->setUniformValue("y_texture", GLint(0));
		program->setUniformValue("u_texture", GLint(1));
		program->setUniformValue("v_texture", GLint(2));
	}
	const auto rect = TransformRect(
		widget->rect(),
		_viewport,
		_factor);
	std::array<std::array<GLfloat, 2>, 4> texcoords = { {
		{ { 0.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, 0.f } },
		{ { 0.f, 0.f } },
	} };
	if (const auto shift = (data.rotation / 90); shift != 0) {
		std::rotate(
			begin(texcoords),
			begin(texcoords) + shift,
			end(texcoords));
	}

	const auto width = widget->parentWidget()->width();
	const auto left = (_owner->_topControlsAlignment == style::al_left);
	validateShadowImage();
	const auto position = left
		? QPoint()
		: QPoint(width - st::callTitleShadowRight.width(), 0);
	const auto translated = position - widget->pos();
	const auto shadowArea = QRect(translated, st::callTitleShadowLeft.size());
	const auto shadow = _controlsShadowImage.texturedRect(
		shadowArea,
		(left ? _controlsShadowLeft : _controlsShadowRight),
		widget->rect());
	const auto shadowRect = TransformRect(
		shadow.geometry,
		_viewport,
		_factor);

	const GLfloat coords[] = {
		rect.left(), rect.top(),
		texcoords[0][0], texcoords[0][1],

		rect.right(), rect.top(),
		texcoords[1][0], texcoords[1][1],

		rect.right(), rect.bottom(),
		texcoords[2][0], texcoords[2][1],

		rect.left(), rect.bottom(),
		texcoords[3][0], texcoords[3][1],

		shadowRect.left(), shadowRect.top(),
		shadow.texture.left(), shadow.texture.bottom(),

		shadowRect.right(), shadowRect.top(),
		shadow.texture.right(), shadow.texture.bottom(),

		shadowRect.right(), shadowRect.bottom(),
		shadow.texture.right(), shadow.texture.top(),

		shadowRect.left(), shadowRect.bottom(),
		shadow.texture.left(), shadow.texture.top(),
	};

	_contentBuffer->write(0, coords, sizeof(coords));

	const auto bottomShadowArea = QRect(
		0,
		widget->parentWidget()->height() - st::callBottomShadowSize,
		widget->parentWidget()->width(),
		st::callBottomShadowSize);
	const auto bottomShadowFill = bottomShadowArea.intersected(
		widget->geometry()).translated(-widget->pos());
	const auto shadowHeight = bottomShadowFill.height();
	const auto shadowAlpha = (shadowHeight * kBottomShadowAlphaMax)
		/ (st::callBottomShadowSize * 255.);

	program->setUniformValue("viewport", _uniformViewport);
	program->setUniformValue("shadow", QVector3D(
		shadowHeight * _factor,
		TransformRect(bottomShadowFill, _viewport, _factor).bottom(),
		shadowAlpha));

	FillTexturedRectangle(f, &*program);

#ifndef Q_OS_MAC
	if (!shadowRect.empty()) {
		f.glEnable(GL_BLEND);
		f.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		const auto guard = gsl::finally([&] {
			f.glDisable(GL_BLEND);
		});

		_imageProgram->bind();
		_imageProgram->setUniformValue("viewport", _uniformViewport);
		_imageProgram->setUniformValue("s_texture", GLint(0));

		f.glActiveTexture(GL_TEXTURE0);
		_controlsShadowImage.bind(f);

		FillTexturedRectangle(f, &*_imageProgram, 4);
	}
#endif // Q_OS_MAC
}

void Panel::Incoming::RendererGL::validateShadowImage() {
	if (_controlsShadowImage) {
		return;
	}
	const auto size = st::callTitleShadowLeft.size();
	const auto full = QSize(size.width(), 2 * size.height()) * int(_factor);
	auto image = QImage(full, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(_factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		st::callTitleShadowLeft.paint(p, 0, 0, size.width());
		_controlsShadowLeft = QRect(0, 0, full.width(), full.height() / 2);
		st::callTitleShadowRight.paint(p, 0, size.height(), size.width());
		_controlsShadowRight = QRect(
			0,
			full.height() / 2,
			full.width(),
			full.height() / 2);
	}
	_controlsShadowImage.setImage(std::move(image));
}

void Panel::Incoming::RendererGL::uploadTexture(
		QOpenGLFunctions &f,
		GLint internalformat,
		GLint format,
		QSize size,
		QSize hasSize,
		int stride,
		const void *data) const {
	f.glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
	if (hasSize != size) {
		f.glTexImage2D(
			GL_TEXTURE_2D,
			0,
			internalformat,
			size.width(),
			size.height(),
			0,
			format,
			GL_UNSIGNED_BYTE,
			data);
	} else {
		f.glTexSubImage2D(
			GL_TEXTURE_2D,
			0,
			0,
			0,
			size.width(),
			size.height(),
			format,
			GL_UNSIGNED_BYTE,
			data);
	}
	f.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

Panel::Incoming::RendererSW::RendererSW(not_null<Incoming*> owner)
: _owner(owner) {
	initBottomShadow();
}

void Panel::Incoming::RendererSW::paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) {
	const auto markGuard = gsl::finally([&] {
		_owner->_track->markFrameShown();
	});
	const auto data = _owner->_track->frameWithInfo(true);
	const auto &image = data.original;
	const auto rotation = data.rotation;
	if (image.isNull()) {
		p.fillRect(clip.boundingRect(), Qt::black);
	} else {
		const auto rect = _owner->widget()->rect();
		using namespace Media::View;
		auto hq = PainterHighQualityEnabler(p);
		if (UsePainterRotation(rotation)) {
			if (rotation) {
				p.save();
				p.rotate(rotation);
			}
			p.drawImage(RotatedRect(rect, rotation), image);
			if (rotation) {
				p.restore();
			}
		} else if (rotation) {
			p.drawImage(rect, RotateFrameImage(image, rotation));
		} else {
			p.drawImage(rect, image);
		}
		fillBottomShadow(p);
		fillTopShadow(p);
	}
}

void Panel::Incoming::RendererSW::initBottomShadow() {
	auto image = QImage(
		QSize(1, st::callBottomShadowSize) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	const auto colorFrom = uint32(0);
	const auto colorTill = uint32(kBottomShadowAlphaMax);
	const auto rows = image.height();
	const auto step = (uint64(colorTill - colorFrom) << 32) / rows;
	auto accumulated = uint64();
	auto bytes = image.bits();
	for (auto y = 0; y != rows; ++y) {
		accumulated += step;
		const auto color = (colorFrom + uint32(accumulated >> 32)) << 24;
		for (auto x = 0; x != image.width(); ++x) {
			*(reinterpret_cast<uint32*>(bytes) + x) = color;
		}
		bytes += image.bytesPerLine();
	}
	_bottomShadow = std::move(image);
}

void Panel::Incoming::RendererSW::fillTopShadow(QPainter &p) {
#ifndef Q_OS_MAC
	const auto widget = _owner->widget();
	const auto width = widget->parentWidget()->width();
	const auto left = (_owner->_topControlsAlignment == style::al_left);
	const auto &icon = left
		? st::callTitleShadowLeft
		: st::callTitleShadowRight;
	const auto position = left
		? QPoint()
		: QPoint(width - icon.width(), 0);
	const auto shadowArea = QRect(position, icon.size());
	const auto fill = shadowArea.intersected(
		widget->geometry()).translated(-widget->pos());
	if (fill.isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(fill);
	icon.paint(p, position - widget->pos(), width);
	p.restore();
#endif // Q_OS_MAC
}

void Panel::Incoming::RendererSW::fillBottomShadow(QPainter &p) {
	const auto widget = _owner->widget();
	const auto shadowArea = QRect(
		0,
		widget->parentWidget()->height() - st::callBottomShadowSize,
		widget->parentWidget()->width(),
		st::callBottomShadowSize);
	const auto fill = shadowArea.intersected(
		widget->geometry()).translated(-widget->pos());
	if (fill.isEmpty()) {
		return;
	}
	const auto factor = cIntRetinaFactor();
	p.drawImage(
		fill,
		_bottomShadow,
		QRect(
			0,
			(factor
				* (fill.y() - shadowArea.translated(-widget->pos()).y())),
			factor,
			factor * fill.height()));
}

Panel::Incoming::Incoming(
	not_null<QWidget*> parent,
	not_null<Webrtc::VideoTrack*> track,
	Ui::GL::Backend backend)
: _surface(Ui::GL::CreateSurface(parent, chooseRenderer(backend)))
, _track(track) {
	widget()->setAttribute(Qt::WA_OpaquePaintEvent);
	widget()->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<QWidget*> Panel::Incoming::widget() const {
	return _surface->rpWidget();
}

not_null<Ui::RpWidgetWrap*> Panel::Incoming::rp() const {
	return _surface.get();
}

void Panel::Incoming::setControlsAlignment(style::align align) {
	if (_topControlsAlignment != align) {
		_topControlsAlignment = align;
		widget()->update();
	}
}

Ui::GL::ChosenRenderer Panel::Incoming::chooseRenderer(
		Ui::GL::Backend backend) {
	_opengl = (backend == Ui::GL::Backend::OpenGL);
	return {
		.renderer = (_opengl
			? std::unique_ptr<Ui::GL::Renderer>(
				std::make_unique<RendererGL>(this))
			: std::make_unique<RendererSW>(this)),
		.backend = backend,
	};
}

} // namespace Calls
