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
#include "calls/group/calls_group_members_row.h"
#include "ui/gl/gl_shader.h"
#include "data/data_peer.h"
#include "styles/style_calls.h"

#include <QtGui/QOpenGLShader>

namespace Calls::Group {
namespace {

using namespace Ui::GL;

// Depends on FragmetSampleTexture().
[[nodiscard]] ShaderPart FragmentFrameColor() {
	return {
		.header = R"(
uniform vec4 frameBg;
uniform vec3 shadow; // fullHeight, shown, maxOpacity
float insideTexture() {
	vec2 textureHalf = vec2(0.5, 0.5);
	vec2 fromTextureCenter = abs(v_texcoord - textureHalf);
	vec2 fromTextureEdge = max(fromTextureCenter, textureHalf) - textureHalf;
	float outsideCheck = dot(fromTextureEdge, fromTextureEdge);
	return step(outsideCheck, 0);
}
)",
		.body = R"(
	float inside = insideTexture();
	result = result * inside + frameBg * (1. - inside);

	float shadowCoord = gl_FragCoord.y - roundRect.y;
	float shadowValue = max(1. - (shadowCoord / shadow.x), 0.);
	float shadowShown = shadowValue * shadow.y * shadow.z;
	result = vec4(result.rgb * (1. - shadowShown), result.a);
)",
	};
}

void FillRectVertices(GLfloat *coords, Rect rect) {
	coords[0] = coords[10] = rect.left();
	coords[1] = coords[11] = rect.top();
	coords[2] = rect.right();
	coords[3] = rect.top();
	coords[4] = coords[6] = rect.right();
	coords[5] = coords[7] = rect.bottom();
	coords[8] = rect.left();
	coords[9] = rect.bottom();
}

void FillTriangles(
		QOpenGLFunctions &f,
		gsl::span<const GLfloat> coords,
		not_null<QOpenGLBuffer*> buffer,
		not_null<QOpenGLShaderProgram*> program,
		QSize viewportWithFactor,
		const QColor &color,
		Fn<void()> additional = nullptr) {
	Expects(coords.size() % 6 == 0);

	if (coords.empty()) {
		return;
	}
	buffer->bind();
	buffer->allocate(coords.data(), coords.size() * sizeof(GLfloat));

	f.glUseProgram(program->programId());
	program->setUniformValue("viewport", QSizeF(viewportWithFactor));
	program->setUniformValue("s_color", Uniform(color));

	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		2 * sizeof(GLfloat),
		nullptr);
	f.glEnableVertexAttribArray(position);

	if (additional) {
		additional();
	}

	f.glDrawArrays(GL_TRIANGLES, 0, coords.size() / 2);

	f.glDisableVertexAttribArray(position);
}

void FillTexturedRectangle(
		QOpenGLFunctions &f,
		not_null<QOpenGLShaderProgram*> program,
		int skipVertices = 0) {
	const auto shift = [&](int elements) {
		return reinterpret_cast<const void*>(
			(skipVertices * 4 + elements) * sizeof(GLfloat));
	};
	GLint position = program->attributeLocation("position");
	f.glVertexAttribPointer(
		position,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(0));
	f.glEnableVertexAttribArray(position);

	GLint texcoord = program->attributeLocation("texcoord");
	f.glVertexAttribPointer(
		texcoord,
		2,
		GL_FLOAT,
		GL_FALSE,
		4 * sizeof(GLfloat),
		shift(2));
	f.glEnableVertexAttribArray(texcoord);

	f.glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	f.glDisableVertexAttribArray(position);
	f.glDisableVertexAttribArray(texcoord);
}

} // namespace

Viewport::RendererGL::RendererGL(not_null<Viewport*> owner)
: _owner(owner)
, _pinIcon(st::groupCallLargeVideo.pin)
, _muteIcon(st::groupCallLargeVideoCrossLine)
, _pinBackground(
	(st::groupCallLargeVideo.pinPadding.top()
		+ st::groupCallLargeVideo.pin.icon.height()
		+ st::groupCallLargeVideo.pinPadding.bottom()) / 2,
	st::radialBg) {

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_buttons.invalidate();
	}, _lifetime);
}

void Viewport::RendererGL::free(const Textures &textures) {
	_texturesToFree.push_back(textures);
}

void Viewport::RendererGL::init(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_factor = widget->devicePixelRatio();
	_frameBuffer.emplace();
	_frameBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_frameBuffer->create();
	_frameBuffer->bind();
	_frameBuffer->allocate(64 * sizeof(GLfloat));
	_yuv420Program.emplace();
	_frameVertexShader = LinkProgram(
		&*_yuv420Program,
		VertexShader({
			VertexViewportTransform(),
			VertexPassTextureCoord(),
		}),
		FragmentShader({
			FragmentSampleYUV420Texture(),
			FragmentFrameColor(),
			FragmentRoundCorners(),
		})).vertex;

	_bgBuffer.emplace();
	_bgBuffer->setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_bgBuffer->create();
	_bgProgram.emplace();
	LinkProgram(
		&*_bgProgram,
		VertexShader({ VertexViewportTransform() }),
		FragmentShader({ FragmentStaticColor() }));

	_imageProgram.emplace();
	LinkProgram(
		&*_imageProgram,
		_frameVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
		}));
}

void Viewport::RendererGL::ensureARGB32Program() {
	Expects(_frameVertexShader != nullptr);

	_argb32Program.emplace();
	LinkProgram(
		&*_argb32Program,
		_frameVertexShader,
		FragmentShader({
			FragmentSampleARGB32Texture(),
			FragmentFrameColor(),
			FragmentRoundCorners(),
		}));
}

void Viewport::RendererGL::deinit(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_bgBuffer = std::nullopt;
	_frameBuffer = std::nullopt;
	_frameVertexShader = nullptr;
	_bgProgram = std::nullopt;
	_imageProgram = std::nullopt;
	_argb32Program = std::nullopt;
	_yuv420Program = std::nullopt;
	for (const auto &tile : _owner->_tiles) {
		if (const auto textures = tile->takeTextures()) {
			free(textures);
		}
	}
	freeTextures(f);
	_buttons.destroy(f);
}

void Viewport::RendererGL::resize(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f,
		int w,
		int h) {
	_factor = widget->devicePixelRatio();
	_viewport = QSize(w, h);
	f.glViewport(0, 0, w * _factor, h * _factor);
}

void Viewport::RendererGL::paint(
		not_null<QOpenGLWidget*> widget,
		QOpenGLFunctions &f) {
	_factor = widget->devicePixelRatio();
	validateDatas();
	fillBackground(f);
	auto index = 0;
	for (const auto &tile : _owner->_tiles) {
		auto &data = _tileData[_tileDataIndices[index++]];
		validateOutlineAnimation(tile.get(), data);
		paintTile(f, tile.get(), data);
	}
	freeTextures(f);
}

void Viewport::RendererGL::fillBackground(QOpenGLFunctions &f) {
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
		FillRectVertices(coords, transformRect(rect));
		coords += 12;
	}
	FillTriangles(
		f,
		_bgTriangles,
		&*_bgBuffer,
		&*_bgProgram,
		_viewport * _factor,
		st::groupCallBg->c);
}

void Viewport::RendererGL::paintTile(
		QOpenGLFunctions &f,
		not_null<VideoTile*> tile,
		const TileData &tileData) {
	const auto track = tile->track();
	const auto data = track->frameWithInfo(false);
	if (data.format == Webrtc::FrameFormat::None) {
		return;
	}

	const auto geometry = tile->geometry();
	if (geometry.isEmpty()) {
		return;
	}
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto &st = st::groupCallLargeVideo;
	const auto shown = _owner->_controlsShownRatio;
	const auto fullNameShift = st.namePosition.y() + st::normalFont->height;
	const auto nameShift = anim::interpolate(fullNameShift, 0, shown);
	const auto row = tile->row();
	const auto style = row->computeIconState(MembersRowStyle::LargeVideo);

	ensureButtonsImage();

	// Frame.
	const auto expand = !_owner->wide()/* && !tile->screencast()*/;
	const auto scaled = Media::View::FlipSizeByRotation(
		data.yuv420->size,
		data.rotation
	).scaled(
		QSize(width, height),
		(expand ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio));
	const auto good = !scaled.isEmpty();
	const auto left = (width - scaled.width()) / 2;
	const auto top = (height - scaled.height()) / 2;
	const auto right = left + scaled.width();
	const auto bottom = top + scaled.height();
	const auto radius = GLfloat(st::roundRadiusLarge);
	auto dleft = good ? (float(left) / scaled.width()) : 0.f;
	auto dright = good ? (float(width - left) / scaled.width()) : 1.f;
	auto dtop = good ? (float(top) / scaled.height()) : 0.f;
	auto dbottom = good ? (float(height - top) / scaled.height()) : 1.f;
	const auto swap = (((data.rotation / 90) % 2) == 1);
	if (swap) {
		std::swap(dleft, dtop);
		std::swap(dright, dbottom);
	}
	const auto rect = transformRect(geometry);
	auto texCoord = std::array<std::array<GLfloat, 2>, 4> { {
		{ { -dleft, 1.f + dtop } },
		{ { dright, 1.f + dtop } },
		{ { dright, 1.f - dbottom } },
		{ { -dleft, 1.f - dbottom } },
	} };
	if (data.rotation > 0) {
		std::rotate(
			texCoord.begin(),
			texCoord.begin() + (data.rotation / 90),
			texCoord.end());
	}

	// Pin.
	const auto pin = _buttons.texturedRect(
		tile->pinInner().translated(x, y),
		tile->pinned() ? _pinOn : _pinOff,
		geometry);
	const auto pinRect = transformRect(pin.geometry);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = x + width - st.iconPosition.x() - icon.width();
	const auto iconTop = y + (height
		- st.iconPosition.y()
		- icon.height()
		+ nameShift);
	const auto mute = _buttons.texturedRect(
		QRect(iconLeft, iconTop, icon.width(), icon.height()),
		(row->state() == MembersRow::State::Active
			? _muteOff
			: _muteOn),
		geometry);
	const auto muteRect = transformRect(mute.geometry);

	// Name.
	const auto namePosition = QPoint(
		x + st.namePosition.x(),
		y + (height
			- st.namePosition.y()
			- st::semiboldFont->height
			+ nameShift));
	const auto name = _names.texturedRect(
		QRect(namePosition, tileData.nameRect.size() / cIntRetinaFactor()),
		tileData.nameRect,
		geometry);
	const auto nameRect = transformRect(name.geometry);

	const GLfloat coords[] = {
		rect.left(), rect.top(),
		texCoord[0][0], texCoord[0][1],

		rect.right(), rect.top(),
		texCoord[1][0], texCoord[1][1],

		rect.right(), rect.bottom(),
		texCoord[2][0], texCoord[2][1],

		rect.left(), rect.bottom(),
		texCoord[3][0], texCoord[3][1],

		pinRect.left(), pinRect.top(),
		pin.texture.left(), pin.texture.bottom(),

		pinRect.right(), pinRect.top(),
		pin.texture.right(), pin.texture.bottom(),

		pinRect.right(), pinRect.bottom(),
		pin.texture.right(), pin.texture.top(),

		pinRect.left(), pinRect.bottom(),
		pin.texture.left(), pin.texture.top(),

		muteRect.left(), muteRect.top(),
		mute.texture.left(), mute.texture.bottom(),

		muteRect.right(), muteRect.top(),
		mute.texture.right(), mute.texture.bottom(),

		muteRect.right(), muteRect.bottom(),
		mute.texture.right(), mute.texture.top(),

		muteRect.left(), muteRect.bottom(),
		mute.texture.left(), mute.texture.top(),

		nameRect.left(), nameRect.top(),
		name.texture.left(), name.texture.bottom(),

		nameRect.right(), nameRect.top(),
		name.texture.right(), name.texture.bottom(),

		nameRect.right(), nameRect.bottom(),
		name.texture.right(), name.texture.top(),

		nameRect.left(), nameRect.bottom(),
		name.texture.left(), name.texture.top(),
	};

	tile->ensureTexturesCreated(f);
	const auto &textures = tile->textures();
	const auto upload = (textures.trackIndex != data.index);
	const auto uploadOne = [&](
			GLint internalformat,
			GLint format,
			QSize size,
			int stride,
			const void *data) {
		f.glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
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
		f.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	};
	if (upload) {
		textures.textureIndex = 1 - textures.textureIndex;
	}
	const auto rgba = (data.format == Webrtc::FrameFormat::ARGB32);
	if (rgba) {
		ensureARGB32Program();
		f.glUseProgram(_argb32Program->programId());
		f.glActiveTexture(GL_TEXTURE0);
		textures.values.bind(f, textures.textureIndex);
		if (upload) {
			const auto &image = data.original;
			const auto stride = image.bytesPerLine() / 4;
			const auto data = image.constBits();
			uploadOne(GL_RGB, GL_RGBA, image.size(), stride, data);
		}
		_argb32Program->setUniformValue("s_texture", GLint(0));
	} else {
		const auto yuv = data.yuv420;
		const auto otherSize = yuv->chromaSize;
		f.glUseProgram(_yuv420Program->programId());
		f.glActiveTexture(GL_TEXTURE0);
		textures.values.bind(f, textures.textureIndex * 3 + 0);
		if (upload) {
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			uploadOne(GL_RED, GL_RED, yuv->size, yuv->y.stride, yuv->y.data);
		}
		f.glActiveTexture(GL_TEXTURE1);
		textures.values.bind(f, textures.textureIndex * 3 + 1);
		if (upload) {
			uploadOne(GL_RED, GL_RED, otherSize, yuv->u.stride, yuv->u.data);
		}
		f.glActiveTexture(GL_TEXTURE2);
		textures.values.bind(f, textures.textureIndex * 3 + 2);
		if (upload) {
			uploadOne(GL_RED, GL_RED, otherSize, yuv->v.stride, yuv->v.data);
			f.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		}
		_yuv420Program->setUniformValue("y_texture", GLint(0));
		_yuv420Program->setUniformValue("u_texture", GLint(1));
		_yuv420Program->setUniformValue("v_texture", GLint(2));
	}
	tile->track()->markFrameShown();

	_frameBuffer->bind();
	_frameBuffer->write(0, coords, sizeof(coords));

	const auto program = rgba ? &*_argb32Program : &*_yuv420Program;
	const auto uniformViewport = QSizeF(_viewport * _factor);

	program->setUniformValue("viewport", uniformViewport);
	program->setUniformValue(
		"frameBg",
		Uniform(st::groupCallMembersBg->c));
	const auto outline = tileData.outlined.value(tileData.outline ? 1. : 0.);
	program->setUniformValue("radiusOutline", QVector2D(
		radius * _factor,
		(outline > 0) ? (st::groupCallOutline * _factor) : 0.f));
	program->setUniformValue("roundRect", Uniform(rect));
	program->setUniformValue("roundBg", Uniform(st::groupCallBg->c));
	program->setUniformValue("outlineFg", QVector4D(
		st::groupCallMemberActiveIcon->c.redF(),
		st::groupCallMemberActiveIcon->c.greenF(),
		st::groupCallMemberActiveIcon->c.blueF(),
		st::groupCallMemberActiveIcon->c.alphaF() * outline));

	const auto shadowHeight = st.shadowHeight * _factor;
	const auto shadowAlpha = kShadowMaxAlpha / 255.f;
	program->setUniformValue(
		"shadow",
		QVector3D(shadowHeight, shown, shadowAlpha));

	FillTexturedRectangle(f, program);

	const auto pinVisible = _owner->wide()
		&& (pin.geometry.bottom() > y);
	if (nameShift == fullNameShift && !pinVisible) {
		return;
	}

	f.glEnable(GL_BLEND);
	f.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	const auto guard = gsl::finally([&] {
		f.glDisable(GL_BLEND);
	});

	f.glUseProgram(_imageProgram->programId());
	_imageProgram->setUniformValue("viewport", uniformViewport);
	_imageProgram->setUniformValue("s_texture", GLint(0));

	f.glActiveTexture(GL_TEXTURE0);
	_buttons.bind(f);

	if (pinVisible) {
		FillTexturedRectangle(f, &*_imageProgram, 4);
	}

	if (nameShift == fullNameShift) {
		return;
	}

	// Mute.
	if (!muteRect.empty()) {
		FillTexturedRectangle(f, &*_imageProgram, 8);
	}

	// Name.
	if (!nameRect.empty()) {
		_names.bind(f);
		FillTexturedRectangle(f, &*_imageProgram, 12);
	}
}

Rect Viewport::RendererGL::transformRect(const Rect &raster) const {
	return {
		raster.left() * _factor,
		float(_viewport.height() - raster.bottom()) * _factor,
		raster.width() * _factor,
		raster.height() * _factor,
	};
}

Rect Viewport::RendererGL::transformRect(const QRect &raster) const {
	return {
		raster.x() * _factor,
		(_viewport.height() - raster.y() - raster.height()) * _factor,
		raster.width() * _factor,
		raster.height() * _factor,
	};
}

void Viewport::RendererGL::freeTextures(QOpenGLFunctions &f) {
	for (auto &textures : base::take(_texturesToFree)) {
		textures.values.destroy(f);
	}
}

void Viewport::RendererGL::ensureButtonsImage() {
	if (_buttons) {
		return;
	}
	const auto factor = cIntRetinaFactor();
	const auto pinOnSize = VideoTile::PinInnerSize(true);
	const auto pinOffSize = VideoTile::PinInnerSize(false);
	const auto muteSize = st::groupCallLargeVideoCrossLine.icon.size();

	const auto fullSize = QSize(
		std::max({
			pinOnSize.width(),
			pinOffSize.width(),
			2 * muteSize.width(),
		}),
		pinOnSize.height() + pinOffSize.height() + muteSize.height());
	const auto imageSize = fullSize * factor;
	auto image = _buttons.takeImage();
	if (image.size() != imageSize) {
		image = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
	}
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(cRetinaFactor());
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		_pinOn = QRect(QPoint(), pinOnSize * factor);
		VideoTile::PaintPinButton(
			p,
			true,
			0,
			0,
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		_pinOff = QRect(
			QPoint(0, pinOnSize.height()) * factor,
			pinOffSize * factor);
		VideoTile::PaintPinButton(
			p,
			false,
			0,
			pinOnSize.height(),
			fullSize.width(),
			&_pinBackground,
			&_pinIcon);

		const auto muteTop = pinOnSize.height() + pinOffSize.height();
		_muteOn = QRect(QPoint(0, muteTop) * factor, muteSize * factor);
		_muteIcon.paint(p, { 0, muteTop }, 1.);

		_muteOff = QRect(
			QPoint(muteSize.width(), muteTop) * factor,
			muteSize * factor);
		_muteIcon.paint(p, { muteSize.width(), muteTop }, 0.);
	}
	_buttons.setImage(std::move(image));
}

void Viewport::RendererGL::validateDatas() {
	const auto &tiles = _owner->_tiles;
	const auto &st = st::groupCallLargeVideo;
	const auto count = int(tiles.size());
	const auto factor = cIntRetinaFactor();
	const auto nameHeight = st::semiboldFont->height * factor;
	struct Request {
		int index = 0;
		bool updating = false;
	};
	auto requests = std::vector<Request>();
	auto available = _names.image().width();
	for (auto &data : _tileData) {
		data.stale = true;
	}
	_tileDataIndices.resize(count);
	const auto nameWidth = [&](int i) {
		const auto row = tiles[i]->row();
		const auto hasWidth = tiles[i]->geometry().width()
			- st.iconPosition.x()
			- st::groupCallLargeVideoCrossLine.icon.width()
			- st.namePosition.x();
		return std::clamp(row->name().maxWidth(), 1, hasWidth) * factor;
	};
	for (auto i = 0; i != count; ++i) {
		tiles[i]->row()->lazyInitialize(st::groupCallMembersListItem);
		const auto width = nameWidth(i);
		if (width <= 0) {
			continue;
		}
		if (width > available) {
			available = width;
		}
		const auto peer = tiles[i]->row()->peer();
		const auto j = ranges::find(_tileData, peer, &TileData::peer);
		if (j != end(_tileData)) {
			j->stale = false;
			const auto index = (j - begin(_tileData));
			_tileDataIndices[i] = index;
			if (peer->nameVersion != j->nameVersion
				|| width != j->nameRect.width()) {
				const auto nameTop = index * nameHeight;
				j->nameRect = QRect(0, nameTop, width, nameHeight);
				requests.push_back({ .index = i, .updating = true });
			}
		} else {
			_tileDataIndices[i] = -1;
			requests.push_back({ .index = i, .updating = false });
		}
	}
	if (requests.empty()) {
		return;
	}
	auto maybeStaleAfter = begin(_tileData);
	auto maybeStaleEnd = end(_tileData);
	for (auto &request : requests) {
		const auto i = request.index;
		if (_tileDataIndices[i] >= 0) {
			continue;
		}
		const auto peer = tiles[i]->row()->peer();
		auto index = int(_tileData.size());
		maybeStaleAfter = ranges::find(
			maybeStaleAfter,
			maybeStaleEnd,
			true,
			&TileData::stale);
		if (maybeStaleAfter != maybeStaleEnd) {
			index = (maybeStaleAfter - begin(_tileData));
			maybeStaleAfter->peer = peer;
			maybeStaleAfter->stale = false;
			request.updating = true;
		} else {
			// This invalidates maybeStale*, but they're already equal.
			_tileData.push_back({ .peer = peer });
		}
		_tileData[index].nameVersion = peer->nameVersion;
		_tileData[index].nameRect = QRect(
			0,
			index * nameHeight,
			nameWidth(i),
			nameHeight);
		_tileDataIndices[i] = index;
	}
	auto image = _names.takeImage();
	const auto imageSize = QSize(
		available * factor,
		_tileData.size() * nameHeight);
	const auto allocate = (image.size() != imageSize);
	auto paintToImage = allocate
		? QImage(imageSize, QImage::Format_ARGB32_Premultiplied)
		: base::take(image);
	paintToImage.setDevicePixelRatio(factor);
	if (allocate && image.isNull()) {
		paintToImage.fill(Qt::transparent);
	}
	{
		auto p = Painter(&paintToImage);
		if (!image.isNull()) {
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.drawImage(0, 0, image);
			if (paintToImage.width() > image.width()) {
				p.fillRect(
					image.width() / factor,
					0,
					(paintToImage.width() - image.width()) / factor,
					image.height() / factor,
					Qt::transparent);
			}
			if (paintToImage.height() > image.height()) {
				p.fillRect(
					0,
					image.height() / factor,
					paintToImage.width() / factor,
					(paintToImage.height() - image.height()) / factor,
					Qt::transparent);
			}
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		}
		p.setPen(st::groupCallVideoTextFg);
		for (const auto &request : requests) {
			const auto i = request.index;
			const auto index = _tileDataIndices[i];
			const auto &data = _tileData[_tileDataIndices[i]];
			const auto row = tiles[i]->row();
			if (request.updating) {
				p.setCompositionMode(QPainter::CompositionMode_Source);
				p.fillRect(
					0,
					data.nameRect.y() / factor,
					paintToImage.width() / factor,
					nameHeight / factor,
					Qt::transparent);
				p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			}
			row->name().drawLeftElided(
				p,
				0,
				data.nameRect.y() / factor,
				data.nameRect.width() / factor,
				paintToImage.width() / factor);
		}
	}
	_names.setImage(std::move(paintToImage));
}

void Viewport::RendererGL::validateOutlineAnimation(
		not_null<VideoTile*> tile,
		TileData &data) {
	const auto outline = tile->row()->speaking();
	if (data.outline == outline) {
		return;
	}
	data.outline = outline;
	data.outlined.start(
		[=] { _owner->widget()->update(); },
		outline ? 0. : 1.,
		outline ? 1. : 0.,
		st::fadeWrapDuration);
}

} // namespace Calls::Group
