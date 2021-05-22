/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_large_video.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "base/platform/base_platform_info.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
#include "ui/gl/gl_surface.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

#include "core/application.h"

#include <QtGui/QWindow>
#include <QtGui/QOpenGLShader>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLBuffer>

namespace Calls::Group {
namespace {

constexpr auto kShadowMaxAlpha = 80;

const char *FrameVertexShader() {
	return R"(
#version 130
in vec2 position;
in vec2 texcoord;
uniform vec2 viewport;
out vec2 v_texcoord;
vec4 transform(vec2 pos) {
	return vec4(vec2(-1, -1) + 2 * pos / viewport, 0., 1.);
}
void main() {
	gl_Position = transform(position);
	v_texcoord = texcoord;
}
)";
}

const char *FrameFragmentShader() {
	return R"(
#version 130
in vec2 v_texcoord;
uniform sampler2D s_texture;
out vec4 fragColor;
void main() {
	vec4 color = texture(s_texture, v_texcoord);
    fragColor = vec4(color.b, color.g, color.r, color.a);
}
)";
}

const char *FillVertexShader() {
	return R"(
#version 130
in vec2 position;
uniform vec2 viewport;
vec4 transform(vec2 pos) {
	return vec4(vec2(-1, -1) + 2 * pos / viewport, 0., 1.);
}
void main() {
	gl_Position = transform(position);
}
)";
}

const char *FillFragmentShader() {
	return R"(
#version 130
uniform vec4 s_color;
out vec4 fragColor;
void main() {
    fragColor = s_color;
}
)";
}

not_null<QOpenGLShader*> MakeShader(
		not_null<QOpenGLShaderProgram*> program,
		QOpenGLShader::ShaderType type,
		const char *source) {
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
		const char *vertexSource,
		const char *fragmentSource) {
	MakeShader(program, QOpenGLShader::Vertex, vertexSource);
	MakeShader(program, QOpenGLShader::Fragment, fragmentSource);
	if (!program->link()) {
		LOG(("Shader Link Failed: %1.").arg(program->log()));
	}
}

} // namespace

struct LargeVideo::PinButton {
	PinButton(
		not_null<QWidget*> parent,
		const style::GroupCallLargeVideo &st);

	Ui::AbstractButton area;
	Ui::CrossLineAnimation icon;
	Ui::RoundRect background;
	Ui::Text::String text;
	QRect rect;
	Ui::Animations::Simple shownAnimation;
	bool shown = false;
};

class LargeVideo::RendererGL : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<LargeVideo*> owner) : _owner(owner) {
	}
	~RendererGL();

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
	const not_null<LargeVideo*> _owner;

	std::array<GLuint, 3> _textures = {};
	std::optional<QOpenGLBuffer> _frameBuffer;
	std::optional<QOpenGLBuffer> _fillBuffer;
	std::optional<QOpenGLShaderProgram> _frameProgram;
	std::optional<QOpenGLShaderProgram> _fillProgram;
	qint64 _key = 0;

};

LargeVideo::PinButton::PinButton(
	not_null<QWidget*> parent,
	const style::GroupCallLargeVideo &st)
: area(parent)
, icon(st::groupCallLargeVideoPin)
, background(
	(st.pinPadding.top()
		+ st::groupCallLargeVideoPin.icon.height()
		+ st.pinPadding.bottom()) / 2,
	st::radialBg) {
}

LargeVideo::RendererGL::~RendererGL() {
}

void LargeVideo::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	f->glGenTextures(3, _textures.data());
	for (const auto texture : _textures) {
		f->glBindTexture(GL_TEXTURE_2D, texture);
		const auto clamp = GL_CLAMP_TO_EDGE;
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	_frameBuffer.emplace();
	_frameBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_frameBuffer->create();

	_fillBuffer.emplace();
	_fillBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_fillBuffer->create();

	_frameProgram.emplace();
	LinkProgram(&*_frameProgram, FrameVertexShader(), FrameFragmentShader());

	_fillProgram.emplace();
	LinkProgram(&*_fillProgram, FillVertexShader(), FillFragmentShader());
}

void LargeVideo::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	if (_textures.front()) {
		f->glDeleteTextures(_textures.size(), _textures.data());
		ranges::fill(_textures, 0);
	}
	_frameBuffer = std::nullopt;
	_fillBuffer = std::nullopt;
	_frameProgram = std::nullopt;
	_fillProgram = std::nullopt;
}

void LargeVideo::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f,
		int w,
		int h) {
	f->glViewport(0, 0, w, h);
}

void LargeVideo::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) {
	const auto size = _owner->widget()->size();
	if (size.isEmpty()) {
		return;
	}

	const auto bg = st::groupCallMembersBg->c;
	const auto bgvector = QVector4D(
		bg.redF(),
		bg.greenF(),
		bg.blueF(),
		bg.alphaF());

	const auto [image, rotation] = _owner->_track
		? _owner->_track.track->frameOriginalWithRotation()
		: std::pair<QImage, int>();
	if (image.isNull()) {
		f->glClearColor(bgvector[0], bgvector[1], bgvector[2], bgvector[3]);
		f->glClear(GL_COLOR_BUFFER_BIT);
		return;
	}

	const auto scaled = Media::View::FlipSizeByRotation(
		image.size(),
		rotation
	).scaled(size, Qt::KeepAspectRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	const auto right = left + scaled.width();
	const auto bottom = top + scaled.height();

	auto texcoords = std::array<std::array<GLfloat, 2>, 4> { {
		{ {0, 1}},
		{ {1, 1} },
		{ {1, 0} },
		{ {0, 0} },
	} };
	if (rotation > 0) {
		std::rotate(
			texcoords.begin(),
			texcoords.begin() + (rotation / 90),
			texcoords.end());
	}
	const GLfloat vertices[] = {
		float(left), float(top), texcoords[0][0], texcoords[0][1],
		float(right), float(top), texcoords[1][0], texcoords[1][1],
		float(right), float(bottom), texcoords[2][0], texcoords[2][1],
		float(left), float(bottom), texcoords[3][0], texcoords[3][1],
	};

	f->glUseProgram(_frameProgram->programId());
	f->glActiveTexture(GL_TEXTURE0);
	f->glBindTexture(GL_TEXTURE_2D, _textures[0]);
	const auto key = image.cacheKey();
	if (_key != key) {
		_key = key;
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
	_owner->_track.track->markFrameShown();

	_frameBuffer->bind();
	_frameBuffer->allocate(vertices, sizeof(vertices));

	_frameProgram->setUniformValue("viewport", QSizeF(size));
	_frameProgram->setUniformValue("s_texture", GLint(0));

	GLint position = _frameProgram->attributeLocation("position");
	f->glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
	f->glEnableVertexAttribArray(position);

	GLint texcoord = _frameProgram->attributeLocation("texcoord");
	f->glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
	f->glEnableVertexAttribArray(texcoord);

	f->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f->glDisableVertexAttribArray(position);
	f->glDisableVertexAttribArray(texcoord);

	constexpr auto kMaxTriangles = 8;
	auto coordinates = std::array<GLfloat, 6 * kMaxTriangles>{ 0 };
	auto triangles = 0;
	const auto fill = [&](QRect rect) {
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
	};
	if (left > 0) {
		fill({ 0, 0, left, size.height() });
	}
	if (right < size.width()) {
		fill({ right, 0, size.width() - right, size.height() });
	}
	if (top > 0) {
		fill({ 0, 0, size.width(), top });
	}
	if (bottom < size.height()) {
		fill({ 0, bottom, size.width(), size.height() - bottom });
	}
	if (triangles > 0) {
		_fillBuffer->bind();
		_fillBuffer->allocate(coordinates.data(), triangles * 6 * sizeof(GLfloat));

		f->glUseProgram(_fillProgram->programId());
		_fillProgram->setUniformValue("viewport", QSizeF(size));
		_fillProgram->setUniformValue("s_color", bgvector);

		GLint position = _fillProgram->attributeLocation("position");
		f->glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void*)0);
		f->glEnableVertexAttribArray(position);

		f->glDrawArrays(GL_TRIANGLES, 0, triangles * 3);
	}
}

LargeVideo::LargeVideo(
	QWidget *parent,
	const style::GroupCallLargeVideo &st,
	bool visible,
	rpl::producer<LargeVideoTrack> track,
	rpl::producer<bool> pinned)
: _content(Ui::GL::CreateSurface(
	parent,
	[=](Ui::GL::Capabilities capabilities) {
		return chooseRenderer(capabilities);
	}))
, _st(st)
, _pinButton((_st.pinPosition.x() >= 0)
	? std::make_unique<PinButton>(widget(), st)
	: nullptr)
, _smallLayout(!_pinButton) {
	widget()->setVisible(visible);
	if (_smallLayout) {
		widget()->setCursor(style::cur_pointer);
	}
	setup(std::move(track), std::move(pinned));
}

LargeVideo::~LargeVideo() = default;

Ui::GL::ChosenRenderer LargeVideo::chooseRenderer(
		Ui::GL::Capabilities capabilities) {
	class Renderer : public Ui::GL::Renderer {
	public:
		explicit Renderer(not_null<LargeVideo*> owner) : _owner(owner) {
		}

		void paintFallback(
				Painter &&p,
				const QRegion &clip,
				Ui::GL::Backend backend) override {
			_owner->paint(
				p,
				clip.boundingRect(),
				backend == Ui::GL::Backend::OpenGL);
		}

	private:
		const not_null<LargeVideo*> _owner;

	};

	const auto use = Platform::IsMac()
		? true
		: Platform::IsWindows()
		? capabilities.supported
		: capabilities.transparency;
	LOG(("OpenGL: %1 (LargeVideo)").arg(Logs::b(use)));
	if (use) {
		return {
			.renderer = std::make_unique<RendererGL>(this),
			.backend = Ui::GL::Backend::OpenGL,
		};
	}
	return {
		.renderer = std::make_unique<Renderer>(this),
		.backend = Ui::GL::Backend::Raster,
	};
}

void LargeVideo::raise() {
	widget()->raise();
}

void LargeVideo::setVisible(bool visible) {
	widget()->setVisible(visible);
}

void LargeVideo::setGeometry(int x, int y, int width, int height) {
	widget()->setGeometry(x, y, width, height);
	if (width > 0 && height > 0) {
		const auto kMedium = style::ConvertScale(380);
		const auto kSmall = style::ConvertScale(200);
		_requestedQuality = (width > kMedium || height > kMedium)
			? VideoQuality::Full
			: (width > kSmall || height > kSmall)
			? VideoQuality::Medium
			: VideoQuality::Thumbnail;
	}
}

void LargeVideo::setControlsShown(float64 shown) {
	if (_controlsShownRatio == shown) {
		return;
	}
	_controlsShownRatio = shown;
	widget()->update();
	updateControlsGeometry();
}

rpl::producer<bool> LargeVideo::pinToggled() const {
	return _pinButton
		? _pinButton->area.clicks() | rpl::map([=] { return !_pinned; })
		: rpl::never<bool>() | rpl::type_erased();
}

QSize LargeVideo::trackSize() const {
	return _trackSize.current();
}

rpl::producer<QSize> LargeVideo::trackSizeValue() const {
	return _trackSize.value();
}

rpl::producer<VideoQuality> LargeVideo::requestedQuality() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_content->shownValue(),
		_requestedQuality.value()
	) | rpl::filter([=](bool shown, auto) {
		return shown;
	}) | rpl::map(_2);
}

rpl::lifetime &LargeVideo::lifetime() {
	return _content->lifetime();
}

not_null<QWidget*> LargeVideo::widget() const {
	return _content->rpWidget();
}

void LargeVideo::setup(
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned) {
	widget()->setAttribute(Qt::WA_OpaquePaintEvent);

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter && _pinButton) {
			togglePinShown(true);
		} else if (type == QEvent::Leave && _pinButton) {
			togglePinShown(false);
		} else if (type == QEvent::MouseButtonPress
			&& static_cast<QMouseEvent*>(
				e.get())->button() == Qt::LeftButton) {
			_mouseDown = true;
		} else if (type == QEvent::MouseButtonRelease
			&& static_cast<QMouseEvent*>(
				e.get())->button() == Qt::LeftButton
			&& _mouseDown) {
			_mouseDown = false;
			if (!widget()->isHidden()) {
				_clicks.fire({});
			}
		}
	}, _content->lifetime());

	rpl::combine(
		_content->shownValue(),
		std::move(track)
	) | rpl::map([=](bool shown, LargeVideoTrack track) {
		return shown ? track : LargeVideoTrack();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](LargeVideoTrack track) {
		_track = track;
		widget()->update();

		_trackLifetime.destroy();
		if (!track.track) {
			_trackSize = QSize();
			return;
		}
		track.track->renderNextFrame(
		) | rpl::start_with_next([=] {
			const auto size = track.track->frameSize();
			if (size.isEmpty()) {
				track.track->markFrameShown();
			} else {
				_trackSize = size;
			}
			widget()->update();
		}, _trackLifetime);
		if (const auto size = track.track->frameSize(); !size.isEmpty()) {
			_trackSize = size;
		}
	}, _content->lifetime());

	setupControls(std::move(pinned));
}

void LargeVideo::togglePinShown(bool shown) {
	Expects(_pinButton != nullptr);

	if (_pinButton->shown == shown) {
		return;
	}
	_pinButton->shown = shown;
	_pinButton->shownAnimation.start(
		[=] { updateControlsGeometry(); widget()->update(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration);
}

void LargeVideo::setupControls(rpl::producer<bool> pinned) {
	std::move(pinned) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		if (_pinButton) {
			_pinButton->text.setText(
				st::semiboldTextStyle,
				(pinned
					? tr::lng_pinned_unpin
					: tr::lng_pinned_pin)(tr::now));
			updateControlsGeometry();
		}
		widget()->update();
	}, _content->lifetime());

	_content->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry();
	}, _content->lifetime());
}

void LargeVideo::updateControlsGeometry() {
	if (_pinButton) {
		const auto &icon = st::groupCallLargeVideoPin.icon;
		const auto innerWidth = icon.width()
			+ _st.pinTextPosition.x()
			+ _pinButton->text.maxWidth();
		const auto innerHeight = icon.height();
		const auto buttonWidth = _st.pinPadding.left() + innerWidth + _st.pinPadding.right();
		const auto buttonHeight = _st.pinPadding.top() + innerHeight + _st.pinPadding.bottom();
		const auto fullWidth = _st.pinPosition.x() * 2 + buttonWidth;
		const auto fullHeight = _st.pinPosition.y() * 2 + buttonHeight;
		const auto slide = anim::interpolate(
			_st.pinPosition.y() + buttonHeight,
			0,
			_pinButton->shownAnimation.value(_pinButton->shown ? 1. : 0.));
		_pinButton->rect = QRect(
			widget()->width() - _st.pinPosition.x() - buttonWidth,
			_st.pinPosition.y() - slide,
			buttonWidth,
			buttonHeight);
		_pinButton->area.setGeometry(
			widget()->width() - fullWidth,
			-slide,
			fullWidth,
			fullHeight);
	}
}

void LargeVideo::paint(Painter &p, QRect clip, bool opengl) {
	const auto fill = [&](QRect rect) {
		if (rect.intersects(clip)) {
			p.fillRect(rect.intersected(clip), st::groupCallMembersBg);
		}
	};
	const auto [image, rotation] = _track
		? _track.track->frameOriginalWithRotation()
		: std::pair<QImage, int>();
	if (image.isNull()) {
		fill(clip);
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	using namespace Media::View;
	const auto size = widget()->size();
	const auto scaled = FlipSizeByRotation(
		image.size(),
		rotation
	).scaled(size, Qt::KeepAspectRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	const auto target = QRect(QPoint(left, top), scaled);
	if (UsePainterRotation(rotation, opengl)) {
		if (rotation) {
			p.save();
			p.rotate(rotation);
		}
		p.drawImage(RotatedRect(target, rotation), image);
		if (rotation) {
			p.restore();
		}
	} else if (rotation) {
		p.drawImage(target, RotateFrameImage(image, rotation));
	} else {
		p.drawImage(target, image);
	}
	_track.track->markFrameShown();

	if (left > 0) {
		fill({ 0, 0, left, size.height() });
	}
	if (const auto right = left + scaled.width()
		; right < size.width()) {
		fill({ right, 0, size.width() - right, size.height() });
	}
	if (top > 0) {
		fill({ 0, 0, size.width(), top });
	}
	if (const auto bottom = top + scaled.height()
		; bottom < size.height()) {
		fill({ 0, bottom, size.width(), size.height() - bottom });
	}

	paintControls(p, clip);
}

void LargeVideo::paintControls(Painter &p, QRect clip) {
	const auto width = widget()->width();
	const auto height = widget()->height();

	// Pin.
	if (_pinButton && _pinButton->rect.intersects(clip)) {
		const auto &icon = st::groupCallLargeVideoPin.icon;
		_pinButton->background.paint(p, _pinButton->rect);
		_pinButton->icon.paint(
			p,
			_pinButton->rect.marginsRemoved(_st.pinPadding).topLeft(),
			_pinned ? 1. : 0.);
		p.setPen(st::groupCallVideoTextFg);
		_pinButton->text.drawLeft(
			p,
			(_pinButton->rect.x()
				+ _st.pinPadding.left()
				+ icon.width()
				+ _st.pinTextPosition.x()),
			(_pinButton->rect.y()
				+ _st.pinPadding.top()
				+ _st.pinTextPosition.y()),
			_pinButton->text.maxWidth(),
			width);
	}

	const auto fullShift = _st.namePosition.y() + st::normalFont->height;
	const auto shown = _controlsShownRatio;
	if (shown == 0.) {
		return;
	}

	const auto shift = anim::interpolate(fullShift, 0, shown);

	// Shadow.
	if (_shadow.isNull()) {
		_shadow = GenerateShadow(_st.shadowHeight, 0, kShadowMaxAlpha);
	}
	const auto shadowRect = QRect(
		0,
		(height - anim::interpolate(0, _st.shadowHeight, shown)),
		width,
		_st.shadowHeight);
	const auto shadowFill = shadowRect.intersected(clip);
	if (shadowFill.isEmpty()) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowFill,
		_shadow,
		QRect(
			0,
			(shadowFill.y() - shadowRect.y()) * factor,
			_shadow.width(),
			shadowFill.height() * factor));
	_track.row->lazyInitialize(st::groupCallMembersListItem);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = width - _st.iconPosition.x() - icon.width();
	const auto iconTop = (height
		- _st.iconPosition.y()
		- icon.height()
		+ shift);
	_track.row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::LargeVideo);

	// Name.
	p.setPen(st::groupCallVideoTextFg);
	const auto hasWidth = width
		- _st.iconPosition.x() - icon.width()
		- _st.namePosition.x();
	const auto nameLeft = _st.namePosition.x();
	const auto nameTop = (height
		- _st.namePosition.y()
		- st::semiboldFont->height
		+ shift);
	_track.row->name().drawLeftElided(p, nameLeft, nameTop, hasWidth, width);
}

QImage GenerateShadow(
		int height,
		int topAlpha,
		int bottomAlpha,
		QColor color) {
	Expects(topAlpha >= 0 && topAlpha < 256);
	Expects(bottomAlpha >= 0 && bottomAlpha < 256);
	Expects(height * style::DevicePixelRatio() < 65536);

	const auto base = (uint32(color.red()) << 16)
		| (uint32(color.green()) << 8)
		| uint32(color.blue());
	const auto premultiplied = (topAlpha == bottomAlpha) || !base;
	auto result = QImage(
		QSize(1, height * style::DevicePixelRatio()),
		(premultiplied
			? QImage::Format_ARGB32_Premultiplied
			: QImage::Format_ARGB32));
	if (topAlpha == bottomAlpha) {
		color.setAlpha(topAlpha);
		result.fill(color);
		return result;
	}
	constexpr auto kShift = 16;
	constexpr auto kMultiply = (1U << kShift);
	const auto values = std::abs(topAlpha - bottomAlpha);
	const auto rows = uint32(result.height());
	const auto step = (values * kMultiply) / (rows - 1);
	const auto till = rows * uint32(step);
	Assert(result.bytesPerLine() == sizeof(uint32));
	auto ints = reinterpret_cast<uint32*>(result.bits());
	if (topAlpha < bottomAlpha) {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha + (i >> kShift)) << 24);
		}
	} else {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha - (i >> kShift)) << 24);
		}
	}
	if (!premultiplied) {
		result = std::move(result).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	return result;
}

} // namespace Calls::Group
