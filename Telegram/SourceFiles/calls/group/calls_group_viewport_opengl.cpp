/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_opengl.h"

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

[[nodiscard]] ShaderPart VertexPassTextureCoord() {
	return {
		.header = R"(
in vec2 texcoord;
out vec2 v_texcoord;
)",
		.body = R"(
	v_texcoord = texcoord;
)",
	};
}

[[nodiscard]] ShaderPart FragmentSampleTexture() {
	return {
		.header = R"(
in vec2 v_texcoord;
uniform sampler2D s_texture;
)",
		.body = R"(
	result = texture(s_texture, v_texcoord);
	result = vec4(result.b, result.g, result.r, result.a);
)",
	};
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
uniform vec2 viewport;
uniform float roundRadius;
float roundedCorner() {
	vec2 viewportHalf = viewport / 2;
	vec2 fromViewportCenter = abs(gl_FragCoord.xy - viewportHalf);
	vec2 vectorRadius = vec2(roundRadius + 0.5, roundRadius + 0.5);
	vec2 fromCenterWithRadius = fromViewportCenter + vectorRadius;
	vec2 fromRoundingCenter = max(fromCenterWithRadius, viewportHalf)
		- viewportHalf;
	float d = length(fromRoundingCenter) - roundRadius;
	return 1. - smoothstep(0., 1., d);
}
)",
		.body = R"(
	result = vec4(result.r, result.g, result.b, result.a * roundedCorner());
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

class Quads final {
public:
	void fill(QRect rect);
	void paint(
		not_null<QOpenGLFunctions*> f,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewport,
		const QColor &color,
		Fn<void()> additional = nullptr);

private:
	static constexpr auto kMaxTriangles = 8;
	std::array<GLfloat, 6 * kMaxTriangles> coordinates{ 0 };
	int triangles = 0;

};

void Quads::fill(QRect rect) {
	Expects(triangles + 2 <= kMaxTriangles);

	auto i = triangles * 6;
	coordinates[i + 0] = coordinates[i + 10] = rect.x();
	coordinates[i + 1] = coordinates[i + 11] = rect.y();
	coordinates[i + 2] = rect.x() + rect.width();
	coordinates[i + 3] = rect.y();
	coordinates[i + 4] = coordinates[i + 6] = rect.x() + rect.width();
	coordinates[i + 5] = coordinates[i + 7] = rect.y() + rect.height();
	coordinates[i + 8] = rect.x();
	coordinates[i + 9] = rect.y() + rect.height();
	triangles += 2;
}

void Quads::paint(
		not_null<QOpenGLFunctions*> f,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewport,
		const QColor &color,
		Fn<void()> additional) {
	if (!triangles) {
		return;
	}
	buffer->bind();
	buffer->allocate(coordinates.data(), triangles * 6 * sizeof(GLfloat));

	f->glUseProgram(program->programId());
	program->setUniformValue("viewport", QSizeF(viewport));
	program->setUniformValue("s_color", QVector4D(
		color.redF(),
		color.greenF(),
		color.blueF(),
		color.alphaF()));

	GLint position = program->attributeLocation("position");
	f->glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void*)0);
	f->glEnableVertexAttribArray(position);

	if (additional) {
		additional();
	}

	f->glDrawArrays(GL_TRIANGLES, 0, triangles * 3);

	f->glDisableVertexAttribArray(position);
}

} // namespace

Viewport::RendererGL::RendererGL(not_null<Viewport*> owner)
: _owner(owner) {
}

void Viewport::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
}

void Viewport::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
}

void Viewport::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f,
		int w,
		int h) {
}

void Viewport::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
}

} // namespace Calls::Group
