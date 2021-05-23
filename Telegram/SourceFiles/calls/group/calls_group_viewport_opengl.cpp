/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_opengl.h"

#include "calls/group/calls_group_viewport_tile.h"
#include "webrtc/webrtc_video_track.h"
#include "media/view/media_view_pip.h"

#include <QtGui/QOpenGLShader>

namespace Calls::Group {
namespace {

struct ShaderPart {
	QString header;
	QString body;
};

[[nodiscard]] QString VertexShader(const std::vector<ShaderPart> &parts) {
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return R"(
#version 130
in vec2 position;
)" + accumulate(&ShaderPart::header) + R"(
void main() {
	vec4 result = vec4(position, 0., 1.);
)" + accumulate(&ShaderPart::body) + R"(
	gl_Position = result;
}
)";
}

[[nodiscard]] QString FragmentShader(const std::vector<ShaderPart> &parts) {
	const auto accumulate = [&](auto proj) {
		return ranges::accumulate(parts, QString(), std::plus<>(), proj);
	};
	return R"(
#version 130
out vec4 fragColor;
)" + accumulate(&ShaderPart::header) + R"(
void main() {
	vec4 result = vec4(0., 0., 0., 0.);
)" + accumulate(&ShaderPart::body) + R"(
	fragColor = result;
}
)";
}

[[nodiscard]] ShaderPart VertexViewportTransform() {
	return {
		.header = R"(
uniform vec2 viewport;
vec4 transform(vec4 position) {
	return vec4(
		vec2(-1, -1) + 2 * position.xy / viewport,
		position.z,
		position.w);
}
)",
		.body = R"(
	result = transform(result);
)",
	};
}

[[nodiscard]] ShaderPart FragmentRoundCorners() {
	return {
		.header = R"(
uniform vec4 roundRect;
uniform vec4 roundBg;
uniform float roundRadius;
float roundedCorner() {
	vec2 rectHalf = roundRect.zw / 2;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(gl_FragCoord.xy - rectCenter);
	vec2 vectorRadius = vec2(roundRadius + 0.5, roundRadius + 0.5);
	vec2 fromCenterWithRadius = fromRectCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, rectHalf)
		- rectHalf;
	float d = length(fromRoundingCenter) - roundRadius;
	return 1. - smoothstep(0., 1., d);
}
)",
		.body = R"(
	float rounded = roundedCorner();
	result = result * rounded + roundBg * (1. - rounded);
)",
	};
}

[[nodiscard]] ShaderPart FragmentFrameColor() {
	return {
		.header = R"(
uniform sampler2D s_texture;
uniform vec4 textureRect;
uniform vec4 frameBg;
)",
		.body = R"(
	vec2 texturePos = gl_FragCoord.xy - textureRect.xy;
	vec2 textureCoord = vec2(texturePos.x, textureRect.w - texturePos.y)
		/ textureRect.zw;
	vec2 textureHalf = textureRect.zw / 2;
	vec2 fromTextureCenter = abs(texturePos - textureHalf);
	vec2 fromTextureEdge = max(fromTextureCenter, textureHalf) - textureHalf;
	float outsideCheck = dot(fromTextureEdge, fromTextureEdge);
	float inside = step(outsideCheck, 0);
	result = texture(s_texture, textureCoord);
	result = vec4(result.b, result.g, result.r, result.a);
	result = result * inside + frameBg * (1. - inside);
)",
	};
}

[[nodiscard]] ShaderPart FragmentStaticColor() {
	return {
		.header = R"(
uniform vec4 s_color;
)",
		.body = R"(
	result = s_color;
)",
	};
}

not_null<QOpenGLShader*> MakeShader(
		not_null<QOpenGLShaderProgram*> program,
		QOpenGLShader::ShaderType type,
		const QString &source) {
	const auto result = new QOpenGLShader(type, program);
	if (!result->compileSourceCode(source)) {
		LOG(("Shader Compilation Failed: %1, error %2."
			).arg(source
			).arg(result->log()));
	}
	program->addShader(result);
	return result;
}

void LinkProgram(
		not_null<QOpenGLShaderProgram*> program,
		const QString &vertexSource,
		const QString &fragmentSource) {
	MakeShader(program, QOpenGLShader::Vertex, vertexSource);
	MakeShader(program, QOpenGLShader::Fragment, fragmentSource);
	if (!program->link()) {
		LOG(("Shader Link Failed: %1.").arg(program->log()));
	}
}

[[nodiscard]] QVector4D Uniform(const QRect &rect) {
	return QVector4D(rect.x(), rect.y(), rect.width(), rect.height());
}

[[nodiscard]] QVector4D Uniform(const QColor &color) {
	return QVector4D(
		color.redF(),
		color.greenF(),
		color.blueF(),
		color.alphaF());
}

void FillRectVertices(GLfloat *coords, QRect rect) {
	coords[0] = coords[10] = rect.x();
	coords[1] = coords[11] = rect.y();
	coords[2] = rect.x() + rect.width();
	coords[3] = rect.y();
	coords[4] = coords[6] = rect.x() + rect.width();
	coords[5] = coords[7] = rect.y() + rect.height();
	coords[8] = rect.x();
	coords[9] = rect.y() + rect.height();
}

void FillTriangles(
		not_null<QOpenGLFunctions*> f,
		gsl::span<const GLfloat> coords,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewport,
		const QColor &color,
		Fn<void()> additional = nullptr) {
	Expects(coords.size() % 6 == 0);

	if (coords.empty()) {
		return;
	}
	buffer->bind();
	buffer->allocate(coords.data(), coords.size() * sizeof(GLfloat));

	f->glUseProgram(program->programId());
	program->setUniformValue("viewport", QSizeF(viewport));
	program->setUniformValue("s_color", Uniform(color));

	GLint position = program->attributeLocation("position");
	f->glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f->glEnableVertexAttribArray(position);

	if (additional) {
		additional();
	}

	f->glDrawArrays(GL_TRIANGLES, 0, coords.size() / 2);

	f->glDisableVertexAttribArray(position);
}

} // namespace

Viewport::RendererGL::RendererGL(not_null<Viewport*> owner)
: _owner(owner) {
}

void Viewport::RendererGL::free(const Textures &textures) {
	_texturesToFree.push_back(textures);
}

void Viewport::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	_frameBuffer.emplace();
	_frameBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_frameBuffer->create();
	_frameProgram.emplace();
	LinkProgram(
		&*_frameProgram,
		VertexShader({
			VertexViewportTransform(),
		}),
		FragmentShader({
			FragmentFrameColor(),
			FragmentRoundCorners(),
		}));

	_bgBuffer.emplace();
	_bgBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_bgBuffer->create();
	_bgProgram.emplace();
	LinkProgram(
		&*_bgProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({ FragmentStaticColor() }));
}

void Viewport::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	_frameBuffer = std::nullopt;
	_bgBuffer = std::nullopt;
	_frameProgram = std::nullopt;
	_bgProgram = std::nullopt;
	for (const auto &tile : _owner->_tiles) {
		if (const auto textures = tile->takeTextures()) {
			free(textures);
		}
	}
	freeTextures(f);
}

void Viewport::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f,
		int w,
		int h) {
	_viewport = QSize(w, h);
	f->glViewport(0, 0, w, h);
}

void Viewport::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	fillBackground(f);
	for (const auto &tile : _owner->_tiles) {
		paintTile(f, tile.get());
	}
	freeTextures(f);
}

void Viewport::RendererGL::fillBackground(not_null<QOpenGLFunctions*> f) {
	const auto radius = st::roundRadiusLarge;
	const auto radiuses = QMargins{ radius, radius, radius, radius };
	auto bg = QRegion(QRect(QPoint(), _viewport));
	for (const auto &tile : _owner->_tiles) {
		bg -= tile->geometry().marginsRemoved(radiuses);
	}
	if (bg.isEmpty()) {
		return;
	}
	_bgTriangles.resize((bg.end() - bg.begin()) * 12);
	auto coords = _bgTriangles.data();
	for (const auto rect : bg) {
		FillRectVertices(coords, rect);
		coords += 12;
	}
	FillTriangles(
		f,
		_bgTriangles,
		&*_bgBuffer,
		&*_bgProgram,
		_viewport,
		st::groupCallBg->c);
}

void Viewport::RendererGL::paintTile(
		not_null<QOpenGLFunctions*> f,
		not_null<VideoTile*> tile) {
	const auto track = tile->track();
	const auto data = track->frameWithInfo();
	const auto &image = data.original;
	if (image.isNull()) {
		return;
	}

	const auto geometry = tile->geometry();
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto scaled = Media::View::FlipSizeByRotation(
		image.size(),
		data.rotation
	).scaled(QSize(width, height), Qt::KeepAspectRatio);
	const auto left = (width - scaled.width()) / 2;
	const auto top = (height - scaled.height()) / 2;
	const auto right = left + scaled.width();
	const auto bottom = top + scaled.height();
	const auto radius = GLfloat(st::roundRadiusLarge * cIntRetinaFactor());
	// #TODO rotation
	//if (data.rotation > 0) {
	//	std::rotate(
	//		texcoords.begin(),
	//		texcoords.begin() + (data.rotation / 90),
	//		texcoords.end());
	//}
	const GLfloat coords[] = {
		float(x), float(y),
		float(x + width), float(y),
		float(x + width), float(y + height),
		float(x), float(y + height),
	};

	tile->ensureTexturesCreated(f);
	const auto &textures = tile->textures();
	const auto upload = (textures.trackIndex != data.index);
	if (upload) {
		textures.textureIndex = 1 - textures.textureIndex;
	}
	const auto texture = textures.values[textures.textureIndex];

	f->glUseProgram(_frameProgram->programId());
	f->glActiveTexture(GL_TEXTURE0);
	f->glBindTexture(GL_TEXTURE_2D, texture);
	if (upload) {
		f->glPixelStorei(GL_UNPACK_ROW_LENGTH, image.bytesPerLine() / 4);
		f->glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGB,
			image.width(),
			image.height(),
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			image.constBits());
		f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	tile->track()->markFrameShown();

	_frameBuffer->bind();
	_frameBuffer->allocate(coords, sizeof(coords));

	_frameProgram->setUniformValue("viewport", QSizeF(_viewport));
	_frameProgram->setUniformValue("s_texture", GLint(0));
	_frameProgram->setUniformValue(
		"textureRect",
		Uniform(QRect(x + left, y + top, scaled.width(), scaled.height())));
	_frameProgram->setUniformValue(
		"frameBg",
		Uniform(st::groupCallMembersBg->c));
	_frameProgram->setUniformValue("roundRadius", radius);
	_frameProgram->setUniformValue("roundRect", Uniform(geometry));
	_frameProgram->setUniformValue("roundBg", Uniform(st::groupCallBg->c));

	GLint position = _frameProgram->attributeLocation("position");
	f->glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f->glEnableVertexAttribArray(position);

	f->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f->glDisableVertexAttribArray(position);
}

void Viewport::RendererGL::freeTextures(not_null<QOpenGLFunctions*> f) {
	for (const auto &textures : base::take(_texturesToFree)) {
		f->glDeleteTextures(textures.values.size(), textures.values.data());
	}
}

} // namespace Calls::Group
