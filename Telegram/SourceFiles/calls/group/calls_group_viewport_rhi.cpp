/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport_rhi.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "calls/group/calls_group_viewport_tile.h"
#include "calls/group/calls_group_members_row.h"
#include "webrtc/webrtc_video_track.h"
#include "media/view/media_view_pip.h"
#include "media/streaming/media_streaming_utility.h"
#include "ui/rhi/rhi_shader.h"
#include "ui/painter.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"
#include "styles/style_media_view.h"

#include <rhi/qrhi.h>

namespace Calls::Group {
namespace {

using namespace Ui::GL;

constexpr auto kNoiseTextureSize = 256;
constexpr auto kBlurTextureSizeFactor = 4.;
constexpr auto kBlurOpacity = 0.65f;

struct GroupFrameUniforms {
	float viewport[2];
	float _pad0[2];
	float frameBg[4];
	float shadow[4];
	float paused;
	float _pad1[3];
	float roundRect[4];
	float radiusOutline[2];
	float _pad2[2];
	float roundBg[4];
	float outlineFg[4];
};
static_assert(sizeof(GroupFrameUniforms) == 128);

struct BlurUniforms {
	float texelOffset;
	float _pad[3];
};
static_assert(sizeof(BlurUniforms) == 16);

struct ImageUniforms {
	float viewport[2];
	float g_opacity;
	float _pad;
};
static_assert(sizeof(ImageUniforms) == 16);

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Ui::Rhi::ShaderFromFile(
		u":/shaders/"_q + name + u".qsb"_q);
}

[[nodiscard]] bool UseExpandForCamera(QSize original, QSize viewport) {
	using namespace ::Media::Streaming;
	return DecideFrameResize(viewport, original).expanding;
}

[[nodiscard]] QSize NonEmpty(QSize size) {
	return QSize(std::max(size.width(), 1), std::max(size.height(), 1));
}

[[nodiscard]] QSize CountBlurredSize(
		QSize unscaled,
		QSize outer,
		float factor) {
	factor *= kBlurTextureSizeFactor;
	const auto area = outer / int(base::SafeRound(factor * cScale() / 100));
	const auto scaled = unscaled.scaled(area, Qt::KeepAspectRatio);
	return (scaled.width() > unscaled.width()
		|| scaled.height() > unscaled.height())
		? unscaled
		: NonEmpty(scaled);
}

[[nodiscard]] QSize InterpolateScaledSize(
		QSize unscaled,
		QSize size,
		float64 ratio) {
	if (ratio == 0.) {
		return NonEmpty(unscaled.scaled(size, Qt::KeepAspectRatio));
	} else if (ratio == 1.) {
		return NonEmpty(unscaled.scaled(
			size,
			Qt::KeepAspectRatioByExpanding));
	}
	const auto notExpanded = NonEmpty(unscaled.scaled(
		size,
		Qt::KeepAspectRatio));
	const auto expanded = NonEmpty(unscaled.scaled(
		size,
		Qt::KeepAspectRatioByExpanding));
	return QSize(
		anim::interpolate(notExpanded.width(), expanded.width(), ratio),
		anim::interpolate(notExpanded.height(), expanded.height(), ratio));
}

[[nodiscard]] std::array<std::array<float, 2>, 4> CountTexCoords(
		QSize unscaled,
		QSize size,
		float64 expandRatio,
		bool swap = false) {
	const auto scaled = InterpolateScaledSize(unscaled, size, expandRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	auto dleft = float(left) / scaled.width();
	auto dright = float(size.width() - left) / scaled.width();
	auto dtop = float(top) / scaled.height();
	auto dbottom = float(size.height() - top) / scaled.height();
	if (swap) {
		std::swap(dleft, dtop);
		std::swap(dright, dbottom);
	}
	return { {
		{ { -dleft, 1.f + dtop } },
		{ { dright, 1.f + dtop } },
		{ { dright, 1.f - dbottom } },
		{ { -dleft, 1.f - dbottom } },
	} };
}

} // namespace

Viewport::RendererRhi::RendererRhi(not_null<Viewport*> owner)
: _owner(owner)
, _pinIcon(st::groupCallVideoTile.pin)
, _muteIcon(st::groupCallVideoCrossLine)
, _pinBackground(
	(st::groupCallVideoTile.pinPadding.top()
		+ st::groupCallVideoTile.pin.icon.height()
		+ st::groupCallVideoTile.pinPadding.bottom()) / 2,
	st::radialBg) {

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_buttons.invalidate();
	}, _lifetime);
}

Viewport::RendererRhi::~RendererRhi() {
	if (_initialized) {
		releaseResources();
	}
}

QColor Viewport::RendererRhi::rhiClearColor() {
	return _owner->_fullscreen
		? QColor(0, 0, 0)
		: _owner->videoStream()
		? st::mediaviewBg->c
		: st::groupCallBg->c;
}

std::optional<QColor> Viewport::RendererRhi::clearColor() {
	return rhiClearColor();
}

void Viewport::RendererRhi::initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_initialized && _rhi == rhi) {
		return;
	}
	releaseResources();

	_rhi = rhi;
	_rt = rt;
	_cb = cb;

	_offscreenVertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		4 * 4 * sizeof(float));
	_offscreenVertexBuffer->create();

	_onscreenVertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		kMaxOnscreenDraws * kOnscreenVertexSlot);
	_onscreenVertexBuffer->create();

	_uniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		kMaxOnscreenDraws * kUniformSlot);
	_uniformBuffer->create();

	_linearSampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_linearSampler->create();

	_nearestSampler = rhi->newSampler(
		QRhiSampler::Nearest,
		QRhiSampler::Nearest,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_nearestSampler->create();

	_noiseRepeatSampler = rhi->newSampler(
		QRhiSampler::Nearest,
		QRhiSampler::Nearest,
		QRhiSampler::None,
		QRhiSampler::Repeat,
		QRhiSampler::Repeat);
	_noiseRepeatSampler->create();

	_placeholderTexture = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
	_placeholderTexture->create();

	createPipelines();
	_initialized = true;
}

void Viewport::RendererRhi::createPipelines() {
	const auto rpDesc = _rt->renderPassDescriptor();

	const auto passthroughVert = LoadShader(u"passthrough.vert"_q);
	const auto argb32Vert = LoadShader(u"argb32.vert"_q);
	const auto groupFrameVert = LoadShader(u"group_frame.vert"_q);
	const auto argb32Frag = LoadShader(u"argb32.frag"_q);
	const auto yuv420Frag = LoadShader(u"yuv420.frag"_q);
	const auto blurHFrag = LoadShader(u"blur_h.frag"_q);
	const auto blurVFrag = LoadShader(u"blur_v.frag"_q);
	const auto groupFrameFrag = LoadShader(u"group_frame.frag"_q);
	const auto controlsFrag = LoadShader(u"controls.frag"_q);

	QRhiVertexInputLayout passthroughLayout;
	passthroughLayout.setBindings({ { 4 * sizeof(float) } });
	passthroughLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});

	QRhiVertexInputLayout argb32Layout;
	argb32Layout.setBindings({ { 4 * sizeof(float) } });
	argb32Layout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});

	QRhiVertexInputLayout groupFrameLayout;
	groupFrameLayout.setBindings({ { 6 * sizeof(float) } });
	groupFrameLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
		{ 0, 2, QRhiVertexInputAttribute::Float2, 4 * sizeof(float) },
	});

	_downscaleArgb32Srb = _rhi->newShaderResourceBindings();
	_downscaleArgb32Srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	_downscaleArgb32Srb->create();

	_downscaleYuv420Srb = _rhi->newShaderResourceBindings();
	_downscaleYuv420Srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	_downscaleYuv420Srb->create();

	_blurHSrb = _rhi->newShaderResourceBindings();
	_blurHSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(BlurUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_nearestSampler),
	});
	_blurHSrb->create();

	{
		auto *tex = _rhi->newTexture(
			QRhiTexture::RGBA8,
			QSize(1, 1),
			1,
			QRhiTexture::RenderTarget);
		tex->create();
		auto colorAtt = QRhiColorAttachment(tex);
		auto *offscreenRT = _rhi->newTextureRenderTarget(
			QRhiTextureRenderTargetDescription(colorAtt));
		_offscreenRpDesc = offscreenRT->newCompatibleRenderPassDescriptor();
		offscreenRT->setRenderPassDescriptor(_offscreenRpDesc);
		offscreenRT->create();
		delete offscreenRT;
		delete tex;
	}

	_downscaleArgb32Pipeline = _rhi->newGraphicsPipeline();
	_downscaleArgb32Pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, argb32Frag },
	});
	_downscaleArgb32Pipeline->setVertexInputLayout(passthroughLayout);
	_downscaleArgb32Pipeline->setTopology(
		QRhiGraphicsPipeline::TriangleStrip);
	_downscaleArgb32Pipeline->setShaderResourceBindings(_downscaleArgb32Srb);
	_downscaleArgb32Pipeline->setRenderPassDescriptor(_offscreenRpDesc);
	_downscaleArgb32Pipeline->create();

	_downscaleYuv420Pipeline = _rhi->newGraphicsPipeline();
	_downscaleYuv420Pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, yuv420Frag },
	});
	_downscaleYuv420Pipeline->setVertexInputLayout(passthroughLayout);
	_downscaleYuv420Pipeline->setTopology(
		QRhiGraphicsPipeline::TriangleStrip);
	_downscaleYuv420Pipeline->setShaderResourceBindings(
		_downscaleYuv420Srb);
	_downscaleYuv420Pipeline->setRenderPassDescriptor(_offscreenRpDesc);
	_downscaleYuv420Pipeline->create();

	_blurHPipeline = _rhi->newGraphicsPipeline();
	_blurHPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, blurHFrag },
	});
	_blurHPipeline->setVertexInputLayout(passthroughLayout);
	_blurHPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_blurHPipeline->setShaderResourceBindings(_blurHSrb);
	_blurHPipeline->setRenderPassDescriptor(_offscreenRpDesc);
	_blurHPipeline->create();

	_blurVPipeline = _rhi->newGraphicsPipeline();
	_blurVPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, blurVFrag },
	});
	_blurVPipeline->setVertexInputLayout(passthroughLayout);
	_blurVPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_blurVPipeline->setShaderResourceBindings(_blurHSrb);
	_blurVPipeline->setRenderPassDescriptor(_offscreenRpDesc);
	_blurVPipeline->create();

	auto *frameSrb = _rhi->newShaderResourceBindings();
	frameSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(GroupFrameUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_noiseRepeatSampler),
	});
	frameSrb->create();
	_perDrawSrbs.push_back(frameSrb);

	_framePipeline = _rhi->newGraphicsPipeline();
	_framePipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, groupFrameVert },
		{ QRhiShaderStage::Fragment, groupFrameFrag },
	});
	_framePipeline->setVertexInputLayout(groupFrameLayout);
	_framePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_framePipeline->setShaderResourceBindings(frameSrb);
	_framePipeline->setRenderPassDescriptor(rpDesc);
	_framePipeline->create();

	auto *controlsSrb = _rhi->newShaderResourceBindings();
	controlsSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ImageUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	controlsSrb->create();
	_perDrawSrbs.push_back(controlsSrb);

	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

	_controlsPipeline = _rhi->newGraphicsPipeline();
	_controlsPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, controlsFrag },
	});
	_controlsPipeline->setVertexInputLayout(argb32Layout);
	_controlsPipeline->setTargetBlends({ blend });
	_controlsPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_controlsPipeline->setShaderResourceBindings(controlsSrb);
	_controlsPipeline->setRenderPassDescriptor(rpDesc);
	_controlsPipeline->create();
}

void Viewport::RendererRhi::releaseResources() {
	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();

	for (auto &entry : _texturePool) {
		delete entry.texture;
	}
	_texturePool.clear();

	for (auto &data : _tileData) {
		delete data.rgbaTexture;
		delete data.yTexture;
		delete data.uTexture;
		delete data.vTexture;
		delete data.convertedTexture;
		delete data.convertedRpDesc;
		delete data.convertedRt;
		delete data.downscaleTexture;
		delete data.downscaleRpDesc;
		delete data.downscaleRt;
		delete data.blurHTexture;
		delete data.blurHRpDesc;
		delete data.blurHRt;
		delete data.blurVTexture;
		delete data.blurVRpDesc;
		delete data.blurVRt;
	}
	_tileData.clear();
	_tileDataIndices.clear();

	delete _downscaleArgb32Pipeline; _downscaleArgb32Pipeline = nullptr;
	delete _downscaleYuv420Pipeline; _downscaleYuv420Pipeline = nullptr;
	delete _blurHPipeline; _blurHPipeline = nullptr;
	delete _blurVPipeline; _blurVPipeline = nullptr;
	delete _framePipeline; _framePipeline = nullptr;
	delete _controlsPipeline; _controlsPipeline = nullptr;

	delete _downscaleArgb32Srb; _downscaleArgb32Srb = nullptr;
	delete _downscaleYuv420Srb; _downscaleYuv420Srb = nullptr;
	delete _blurHSrb; _blurHSrb = nullptr;

	delete _offscreenRpDesc; _offscreenRpDesc = nullptr;
	delete _noiseTexture; _noiseTexture = nullptr;
	delete _offscreenVertexBuffer; _offscreenVertexBuffer = nullptr;
	delete _onscreenVertexBuffer; _onscreenVertexBuffer = nullptr;
	delete _uniformBuffer; _uniformBuffer = nullptr;
	delete _linearSampler; _linearSampler = nullptr;
	delete _nearestSampler; _nearestSampler = nullptr;
	delete _noiseRepeatSampler; _noiseRepeatSampler = nullptr;
	delete _placeholderTexture; _placeholderTexture = nullptr;

	_buttons.destroy();
	_names.destroy();

	_initialized = false;
}

QRhiTexture *Viewport::RendererRhi::acquirePoolTexture(QSize size) {
	if (_nextPoolIndex < int(_texturePool.size())) {
		auto &entry = _texturePool[_nextPoolIndex++];
		if (entry.size == size) {
			return entry.texture;
		}
		delete entry.texture;
		entry.texture = _rhi->newTexture(QRhiTexture::BGRA8, size);
		entry.texture->create();
		entry.size = size;
		return entry.texture;
	}
	auto *tex = _rhi->newTexture(QRhiTexture::BGRA8, size);
	tex->create();
	_texturePool.push_back({ tex, size });
	_nextPoolIndex++;
	return tex;
}

void Viewport::RendererRhi::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	renderOffscreen(rhi, rt, cb);

	_nextOnscreenSlot = 0;
	_onscreenDraws.clear();
	auto *screenRub = _rhi->nextResourceUpdateBatch();
	if (_rub) {
		screenRub->merge(_rub);
	}
	_rub = screenRub;
	collectOnscreenDraws();
	cb->beginPass(rt, *clearColor(), { 1.0f, 0 }, _rub);
	_rub = nullptr;
	issueOnscreenDraws();
	cb->endPass();
}

void Viewport::RendererRhi::renderOffscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	_rhi = rhi;
	_rt = rt;
	_cb = cb;
	_nextPoolIndex = 0;

	const auto size = rt->pixelSize();
	const auto factor = float(_owner->widget()->devicePixelRatioF());
	if (_factor != factor) {
		_factor = factor;
		_ifactor = int(std::ceil(factor));
		_buttons.invalidate();
	}
	_viewport = QSize(
		int(size.width() / _factor),
		int(size.height() / _factor));

	validateDatas();
	ensureNoiseTexture();

	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();

	auto index = 0;
	for (const auto &tile : _owner->_tiles) {
		if (!tile->visible()) {
			index++;
			continue;
		}
		paintTileOffscreen(
			tile.get(),
			_tileData[_tileDataIndices[index++]]);
	}

	if (_owner->_borrowed) {
		// The borrowed host owns the on-screen pass, so prepare draws and
		// flush their buffer updates now; renderOnscreen() issues them in-pass.
		_nextOnscreenSlot = 0;
		_onscreenDraws.clear();
		auto *rub = _rhi->nextResourceUpdateBatch();
		if (_rub) {
			rub->merge(_rub);
		}
		_rub = rub;
		collectOnscreenDraws();
		cb->resourceUpdate(_rub);
		_rub = nullptr;
	}
}

void Viewport::RendererRhi::collectOnscreenDraws() {
	const auto pw = float(_rt->pixelSize().width());
	const auto ph = float(_rt->pixelSize().height());

	auto index = 0;
	for (const auto &tile : _owner->_tiles) {
		if (!tile->visible()) {
			index++;
			continue;
		}
		paintTileOnscreen(
			tile.get(),
			_tileData[_tileDataIndices[index++]],
			pw, ph);
	}
}

void Viewport::RendererRhi::issueOnscreenDraws() {
	const auto pw = float(_rt->pixelSize().width());
	const auto ph = float(_rt->pixelSize().height());
	for (const auto &draw : _onscreenDraws) {
		_cb->setGraphicsPipeline(draw.pipeline);
		_cb->setShaderResources(draw.srb);
		_cb->setViewport({ 0, 0, pw, ph });
		const QRhiCommandBuffer::VertexInput vbuf(
			_onscreenVertexBuffer, draw.vertexOffset);
		_cb->setVertexInput(0, 1, &vbuf);
		_cb->draw(4);
	}
	_onscreenDraws.clear();
}

void Viewport::RendererRhi::renderOnscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	_rhi = rhi;
	_rt = rt;
	_cb = cb;

	// Only used by a borrowed host: issue the draws prepared in
	// renderOffscreen(), now that the host has opened its render pass.
	issueOnscreenDraws();
}

void Viewport::RendererRhi::ensureNoiseTexture() {
	if (_noiseTexture) {
		return;
	}

	auto noiseImage = QImage(
		kNoiseTextureSize,
		kNoiseTextureSize,
		QImage::Format_ARGB32_Premultiplied);
	noiseImage.fill(Qt::transparent);

	_noiseTexture = _rhi->newTexture(
		QRhiTexture::RGBA8,
		QSize(kNoiseTextureSize, kNoiseTextureSize));
	_noiseTexture->create();

	const auto fade = [](float t) {
		return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
	};
	const auto rnm = [](float x, float y) {
		const auto noise = std::sin(
			x * 12.9898f + y * 78.233f) * 43758.5453f;
		return std::array<float, 4>{
			std::fmod(noise, 1.f) * 2.f - 1.f,
			std::fmod(noise * 1.2154f, 1.f) * 2.f - 1.f,
			std::fmod(noise * 1.3453f, 1.f) * 2.f - 1.f,
			std::fmod(noise * 1.4651f, 1.f) * 2.f - 1.f,
		};
	};
	const auto pnoise3D = [&](float px, float py, float pz) {
		const auto unit = 1.f / 256.f;
		const auto half = 0.5f / 256.f;
		const auto pix = unit * std::floor(px) + half;
		const auto piy = unit * std::floor(py) + half;
		const auto piz = unit * std::floor(pz) + half;
		const auto pfx = px - std::floor(px);
		const auto pfy = py - std::floor(py);
		const auto pfz = pz - std::floor(pz);
		const auto perm00 = rnm(pix, piy)[3];
		const auto g000 = rnm(perm00, piz);
		const auto n000 = g000[0]*pfx + g000[1]*pfy + g000[2]*pfz;
		const auto g001 = rnm(perm00, piz + unit);
		const auto n001 = g001[0]*pfx + g001[1]*pfy
			+ g001[2]*(pfz - 1.f);
		const auto perm01 = rnm(pix, piy + unit)[3];
		const auto g010 = rnm(perm01, piz);
		const auto n010 = g010[0]*pfx + g010[1]*(pfy - 1.f)
			+ g010[2]*pfz;
		const auto g011 = rnm(perm01, piz + unit);
		const auto n011 = g011[0]*pfx + g011[1]*(pfy - 1.f)
			+ g011[2]*(pfz - 1.f);
		const auto perm10 = rnm(pix + unit, piy)[3];
		const auto g100 = rnm(perm10, piz);
		const auto n100 = g100[0]*(pfx - 1.f) + g100[1]*pfy
			+ g100[2]*pfz;
		const auto g101 = rnm(perm10, piz + unit);
		const auto n101 = g101[0]*(pfx - 1.f) + g101[1]*pfy
			+ g101[2]*(pfz - 1.f);
		const auto perm11 = rnm(pix + unit, piy + unit)[3];
		const auto g110 = rnm(perm11, piz);
		const auto n110 = g110[0]*(pfx - 1.f) + g110[1]*(pfy - 1.f)
			+ g110[2]*pfz;
		const auto g111 = rnm(perm11, piz + unit);
		const auto n111 = g111[0]*(pfx - 1.f) + g111[1]*(pfy - 1.f)
			+ g111[2]*(pfz - 1.f);
		const auto fx = fade(pfx);
		const auto fy = fade(pfy);
		const auto fz = fade(pfz);
		const auto nx0 = n000 + (n100 - n000) * fx;
		const auto nx1 = n001 + (n101 - n001) * fx;
		const auto nx2 = n010 + (n110 - n010) * fx;
		const auto nx3 = n011 + (n111 - n011) * fx;
		const auto nxy0 = nx0 + (nx2 - nx0) * fy;
		const auto nxy1 = nx1 + (nx3 - nx1) * fy;
		return nxy0 + (nxy1 - nxy0) * fz;
	};

	constexpr auto grainsize = 1.3f;
	constexpr auto rotation = 1.425f;
	const auto sinR = std::sin(rotation);
	const auto cosR = std::cos(rotation);

	auto *noiseData = noiseImage.bits();
	for (int py = 0; py < kNoiseTextureSize; ++py) {
		for (int px = 0; px < kNoiseTextureSize; ++px) {
			auto tx = float(px) / grainsize;
			auto ty = float(py) / grainsize;
			tx -= 0.5f * kNoiseTextureSize / grainsize;
			ty -= 0.5f * kNoiseTextureSize / grainsize;
			const auto rx = tx * cosR - ty * sinR;
			const auto ry = tx * sinR + ty * cosR;
			tx = rx + 0.5f * kNoiseTextureSize / grainsize;
			ty = ry + 0.5f * kNoiseTextureSize / grainsize;
			const auto noise = pnoise3D(tx, ty, 0.f) * 0.5f + 0.5f;
			const auto v = quint8(std::clamp(noise, 0.f, 1.f) * 255.f);
			auto *pixel = noiseData
				+ (py * noiseImage.bytesPerLine())
				+ (px * 4);
			pixel[0] = v;
			pixel[1] = v;
			pixel[2] = v;
			pixel[3] = 255;
		}
	}
	_rub = _rhi->nextResourceUpdateBatch();
	_rub->uploadTexture(
		_noiseTexture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(noiseImage))));
}

void Viewport::RendererRhi::paintTileOffscreen(
		not_null<VideoTile*> tile,
		TileData &tileData) {
	const auto track = tile->track();
	const auto markGuard = gsl::finally([&] {
		tile->track()->markFrameShown();
	});
	const auto data = track->frameWithInfo(false);
	_userpicFrame = (data.format == Webrtc::FrameFormat::None);
	validateUserpicFrame(tile, tileData);
	const auto frameSize = _userpicFrame
		? tileData.userpicFrame.size()
		: data.yuv420
		? data.yuv420->size
		: data.original.size();
	if (frameSize.isEmpty()) {
		return;
	}

	_rgbaFrame = (data.format == Webrtc::FrameFormat::ARGB32)
		|| _userpicFrame;

	const auto geometry = tile->geometry().translated(
		_owner->borrowedOrigin());
	const auto unscaled = Media::View::FlipSizeByRotation(
		frameSize,
		_userpicFrame ? 0 : data.rotation);

	validateOutlineAnimation(tile, tileData);
	validatePausedAnimation(tile, tileData);

	const auto blurSize = CountBlurredSize(
		unscaled,
		geometry.size(),
		_factor);

	uploadFrame(data, tileData);
	if (!_rgbaFrame) {
		drawYuv2RgbPass(tileData, frameSize);
	}
	prepareOffscreenTargets(tileData, blurSize);
	drawDownscalePass(tileData, blurSize);
	drawBlurPass(tileData, blurSize);
}

void Viewport::RendererRhi::paintTileOnscreen(
		not_null<VideoTile*> tile,
		TileData &tileData,
		float pw,
		float ph) {
	const auto data = tile->track()->frameWithInfo(false);
	_userpicFrame = (data.format == Webrtc::FrameFormat::None);
	const auto frameSize = _userpicFrame
		? tileData.userpicFrame.size()
		: data.yuv420
		? data.yuv420->size
		: data.original.size();
	if (frameSize.isEmpty()) {
		return;
	}
	const auto frameRotation = _userpicFrame ? 0 : data.rotation;
	_rgbaFrame = (data.format == Webrtc::FrameFormat::ARGB32)
		|| _userpicFrame;

	const auto geometry = tile->geometry().translated(
		_owner->borrowedOrigin());
	const auto unscaled = Media::View::FlipSizeByRotation(
		frameSize,
		frameRotation);
	const auto blurSize = CountBlurredSize(
		unscaled,
		geometry.size(),
		_factor);

	drawFramePass(tile, tileData, blurSize);
	drawControls(tile, tileData);
}

void Viewport::RendererRhi::uploadFrame(
		const Webrtc::FrameWithInfo &data,
		TileData &tileData) {
	const auto imageIndex = _userpicFrame ? 0 : (data.index + 1);
	const auto upload = (tileData.trackIndex != imageIndex);
	tileData.trackIndex = imageIndex;
	if (!upload) {
		return;
	}

	_rub = _rhi->nextResourceUpdateBatch();

	if (_rgbaFrame) {
		const auto &image = _userpicFrame
			? tileData.userpicFrame
			: data.original;
		if (!tileData.rgbaTexture
			|| tileData.rgbaSize != image.size()) {
			delete tileData.rgbaTexture;
			tileData.rgbaTexture = _rhi->newTexture(
				QRhiTexture::BGRA8, image.size());
			tileData.rgbaTexture->create();
			tileData.rgbaSize = image.size();
		}
		_rub->uploadTexture(
			tileData.rgbaTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(image))));
	} else {
		const auto yuv = data.yuv420;
		if (!tileData.yTexture || tileData.lumaSize != yuv->size) {
			delete tileData.yTexture;
			tileData.yTexture = _rhi->newTexture(
				QRhiTexture::R8, yuv->size);
			tileData.yTexture->create();
			tileData.lumaSize = yuv->size;
		}
		auto yDesc = QRhiTextureSubresourceUploadDescription(
			yuv->y.data,
			yuv->y.stride * yuv->size.height());
		yDesc.setDataStride(yuv->y.stride);
		_rub->uploadTexture(
			tileData.yTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, yDesc)));

		if (!tileData.uTexture
			|| tileData.chromaSize != yuv->chromaSize) {
			delete tileData.uTexture;
			tileData.uTexture = _rhi->newTexture(
				QRhiTexture::R8, yuv->chromaSize);
			tileData.uTexture->create();
			delete tileData.vTexture;
			tileData.vTexture = _rhi->newTexture(
				QRhiTexture::R8, yuv->chromaSize);
			tileData.vTexture->create();
			tileData.chromaSize = yuv->chromaSize;
		}
		auto uDesc = QRhiTextureSubresourceUploadDescription(
			yuv->u.data,
			yuv->u.stride * yuv->chromaSize.height());
		uDesc.setDataStride(yuv->u.stride);
		_rub->uploadTexture(
			tileData.uTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, uDesc)));
		auto vDesc = QRhiTextureSubresourceUploadDescription(
			yuv->v.data,
			yuv->v.stride * yuv->chromaSize.height());
		vDesc.setDataStride(yuv->v.stride);
		_rub->uploadTexture(
			tileData.vTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, vDesc)));
	}
}

void Viewport::RendererRhi::prepareOffscreenTargets(
		TileData &tileData,
		QSize blurSize) {
	if (tileData.blurSize == blurSize
		&& tileData.downscaleRt
		&& tileData.blurHRt
		&& tileData.blurVRt) {
		return;
	}
	tileData.blurSize = blurSize;

	const auto createOffscreenRT = [&](
			QRhiTexture *&tex,
			QRhiTextureRenderTarget *&rt,
			QRhiRenderPassDescriptor *&rpDesc) {
		delete rt;
		delete rpDesc;
		delete tex;
		tex = _rhi->newTexture(
			QRhiTexture::RGBA8, blurSize, 1, QRhiTexture::RenderTarget);
		tex->create();
		auto colorAtt = QRhiColorAttachment(tex);
		rt = _rhi->newTextureRenderTarget(
			QRhiTextureRenderTargetDescription(colorAtt));
		rpDesc = rt->newCompatibleRenderPassDescriptor();
		rt->setRenderPassDescriptor(rpDesc);
		rt->create();
	};
	createOffscreenRT(
		tileData.downscaleTexture,
		tileData.downscaleRt,
		tileData.downscaleRpDesc);
	createOffscreenRT(
		tileData.blurHTexture,
		tileData.blurHRt,
		tileData.blurHRpDesc);
	createOffscreenRT(
		tileData.blurVTexture,
		tileData.blurVRt,
		tileData.blurVRpDesc);
}

void Viewport::RendererRhi::drawYuv2RgbPass(
		TileData &tileData,
		QSize frameSize) {
	if (!tileData.convertedTexture
		|| tileData.convertedSize != frameSize) {
		delete tileData.convertedRt;
		delete tileData.convertedRpDesc;
		delete tileData.convertedTexture;
		tileData.convertedTexture = _rhi->newTexture(
			QRhiTexture::RGBA8,
			frameSize,
			1,
			QRhiTexture::RenderTarget);
		tileData.convertedTexture->create();
		auto colorAtt = QRhiColorAttachment(tileData.convertedTexture);
		tileData.convertedRt = _rhi->newTextureRenderTarget(
			QRhiTextureRenderTargetDescription(colorAtt));
		tileData.convertedRpDesc =
			tileData.convertedRt->newCompatibleRenderPassDescriptor();
		tileData.convertedRt->setRenderPassDescriptor(
			tileData.convertedRpDesc);
		tileData.convertedRt->create();
		tileData.convertedSize = frameSize;
	}

	// Convert with a fixed orientation for every backend; the OpenGL
	// compositing flip is compensated later, in drawFramePass().
	const auto coords = std::array{
		-1.f, -1.f, 0.f, 1.f,
		 1.f, -1.f, 1.f, 1.f,
		-1.f,  1.f, 0.f, 0.f,
		 1.f,  1.f, 1.f, 0.f,
	};

	if (!_rub) {
		_rub = _rhi->nextResourceUpdateBatch();
	}
	_rub->updateDynamicBuffer(
		_offscreenVertexBuffer, 0, coords.size(), coords.data());

	auto *srb = _rhi->newShaderResourceBindings();
	_perDrawSrbs.push_back(srb);
	srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			tileData.yTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			tileData.uTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			tileData.vTexture,
			_linearSampler),
	});
	srb->create();

	_cb->beginPass(
		tileData.convertedRt, Qt::black, { 1.0f, 0 }, _rub);
	_rub = nullptr;
	_cb->setGraphicsPipeline(_downscaleYuv420Pipeline);
	_cb->setShaderResources(srb);
	_cb->setViewport({
		0, 0, float(frameSize.width()), float(frameSize.height()) });
	const QRhiCommandBuffer::VertexInput vbuf(
		_offscreenVertexBuffer, 0);
	_cb->setVertexInput(0, 1, &vbuf);
	_cb->draw(4);
	_cb->endPass();
}

void Viewport::RendererRhi::drawDownscalePass(
		TileData &tileData,
		QSize blurSize) {
	const float w = float(blurSize.width());
	const float h = float(blurSize.height());
	const float coords[] = {
		-1.f, -1.f, 0.f, 1.f,
		 1.f, -1.f, 1.f, 1.f,
		-1.f,  1.f, 0.f, 0.f,
		 1.f,  1.f, 1.f, 0.f,
	};

	if (!_rub) {
		_rub = _rhi->nextResourceUpdateBatch();
	}
	_rub->updateDynamicBuffer(
		_offscreenVertexBuffer, 0, sizeof(coords), coords);

	auto *srb = _rhi->newShaderResourceBindings();
	_perDrawSrbs.push_back(srb);

	// After drawYuv2RgbPass, YUV420 frames have convertedTexture (RGBA).
	// Use ARGB32 pipeline for both cases in the downscale pass.
	auto *srcTexture = _rgbaFrame
		? tileData.rgbaTexture
		: tileData.convertedTexture;
	srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			srcTexture,
			_linearSampler),
	});
	srb->create();

	_cb->beginPass(tileData.downscaleRt, Qt::black, { 1.0f, 0 }, _rub);
	_rub = nullptr;
	_cb->setGraphicsPipeline(_downscaleArgb32Pipeline);
	_cb->setShaderResources(srb);
	_cb->setViewport({ 0, 0, w, h });
	const QRhiCommandBuffer::VertexInput vbuf(
		_offscreenVertexBuffer, 0);
	_cb->setVertexInput(0, 1, &vbuf);
	_cb->draw(4);
	_cb->endPass();
}

void Viewport::RendererRhi::drawBlurPass(
		TileData &tileData,
		QSize blurSize) {
	const float w = float(blurSize.width());
	const float h = float(blurSize.height());
	const float coords[] = {
		-1.f, -1.f, 0.f, 1.f,
		 1.f, -1.f, 1.f, 1.f,
		-1.f,  1.f, 0.f, 0.f,
		 1.f,  1.f, 1.f, 0.f,
	};

	{
		BlurUniforms blurUniforms{};
		blurUniforms.texelOffset = 1.f / w;
		auto *rub = _rhi->nextResourceUpdateBatch();
		rub->updateDynamicBuffer(
			_offscreenVertexBuffer, 0, sizeof(coords), coords);
		rub->updateDynamicBuffer(
			_uniformBuffer, 0, sizeof(blurUniforms), &blurUniforms);

		auto *srb = _rhi->newShaderResourceBindings();
		_perDrawSrbs.push_back(srb);
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer, 0, sizeof(BlurUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				tileData.downscaleTexture,
				_nearestSampler),
		});
		srb->create();

		_cb->beginPass(
			tileData.blurHRt, Qt::black, { 1.0f, 0 }, rub);
		_cb->setGraphicsPipeline(_blurHPipeline);
		_cb->setShaderResources(srb);
		_cb->setViewport({ 0, 0, w, h });
		const QRhiCommandBuffer::VertexInput vbuf(
			_offscreenVertexBuffer, 0);
		_cb->setVertexInput(0, 1, &vbuf);
		_cb->draw(4);
		_cb->endPass();
	}

	{
		BlurUniforms blurUniforms{};
		blurUniforms.texelOffset = 1.f / h;
		auto *rub = _rhi->nextResourceUpdateBatch();
		rub->updateDynamicBuffer(
			_offscreenVertexBuffer, 0, sizeof(coords), coords);
		rub->updateDynamicBuffer(
			_uniformBuffer, 0, sizeof(blurUniforms), &blurUniforms);

		auto *srb = _rhi->newShaderResourceBindings();
		_perDrawSrbs.push_back(srb);
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer, 0, sizeof(BlurUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				tileData.blurHTexture,
				_nearestSampler),
		});
		srb->create();

		_cb->beginPass(
			tileData.blurVRt, Qt::black, { 1.0f, 0 }, rub);
		_cb->setGraphicsPipeline(_blurVPipeline);
		_cb->setShaderResources(srb);
		_cb->setViewport({ 0, 0, w, h });
		const QRhiCommandBuffer::VertexInput vbuf(
			_offscreenVertexBuffer, 0);
		_cb->setVertexInput(0, 1, &vbuf);
		_cb->draw(4);
		_cb->endPass();
	}
}

void Viewport::RendererRhi::drawFramePass(
		not_null<VideoTile*> tile,
		TileData &tileData,
		QSize blurSize) {
	const auto geometry = tile->geometry().translated(
		_owner->borrowedOrigin());

	const auto data = tile->track()->frameWithInfo(false);
	const auto frameSize = _userpicFrame
		? tileData.userpicFrame.size()
		: data.yuv420
		? data.yuv420->size
		: data.original.size();
	if (frameSize.isEmpty()) {
		return;
	}
	const auto frameRotation = _userpicFrame ? 0 : data.rotation;
	const auto unscaled = Media::View::FlipSizeByRotation(
		frameSize,
		frameRotation);
	const auto tileSize = geometry.size();
	const auto swap = (((frameRotation / 90) % 2) == 1);
	const auto expand = isExpanded(tile, unscaled, tileSize);
	const auto animation = tile->animation();
	const auto expandRatio = (animation.ratio >= 0.)
		? countExpandRatio(tile, unscaled, animation)
		: expand
		? 1.
		: 0.;

	auto texCoords = CountTexCoords(unscaled, tileSize, expandRatio, swap);
	auto blurTexCoords = (expandRatio == 1. && !swap)
		? texCoords
		: CountTexCoords(unscaled, tileSize, 1.);

	if (tile->mirror()) {
		std::swap(texCoords[0], texCoords[1]);
		std::swap(texCoords[2], texCoords[3]);
		std::swap(blurTexCoords[0], blurTexCoords[1]);
		std::swap(blurTexCoords[2], blurTexCoords[3]);
	}
	const auto flipY = _rhi->isYUpInFramebuffer();
	if (auto shift = (frameRotation / 90); shift > 0) {
		if (flipY) {
			shift = 4 - shift;
		}
		std::rotate(
			texCoords.begin(),
			texCoords.begin() + shift,
			texCoords.end());
		std::rotate(
			blurTexCoords.begin(),
			blurTexCoords.begin() + shift,
			blurTexCoords.end());
	}

	const auto outline = tileData.outlined.value(
		tileData.outline ? 1. : 0.);
	const auto paused = tileData.paused.value(
		tileData.pause ? 1. : 0.);
	const auto &st = st::groupCallVideoTile;
	const auto fullscreen = _owner->_fullscreen;
	const auto shown = _owner->_controlsShownRatio;

	const auto pw = float(_rt->pixelSize().width());
	const auto ph = float(_rt->pixelSize().height());

	const auto rect = transformRect(geometry);

	const auto tl = flipY ? 3 : 0;
	const auto tr = flipY ? 2 : 1;
	const auto bl = flipY ? 0 : 3;
	const auto br = flipY ? 1 : 2;
	const float frameCoords[] = {
		rect.left(), rect.top(),
		texCoords[tl][0], texCoords[tl][1],
		blurTexCoords[tl][0], blurTexCoords[tl][1],

		rect.right(), rect.top(),
		texCoords[tr][0], texCoords[tr][1],
		blurTexCoords[tr][0], blurTexCoords[tr][1],

		rect.left(), rect.bottom(),
		texCoords[bl][0], texCoords[bl][1],
		blurTexCoords[bl][0], blurTexCoords[bl][1],

		rect.right(), rect.bottom(),
		texCoords[br][0], texCoords[br][1],
		blurTexCoords[br][0], blurTexCoords[br][1],
	};

	GroupFrameUniforms uniforms{};
	uniforms.viewport[0] = pw;
	uniforms.viewport[1] = ph;

	const auto bg = fullscreen ? QColor(0, 0, 0) : rhiClearColor();
	uniforms.frameBg[0] = bg.redF();
	uniforms.frameBg[1] = bg.greenF();
	uniforms.frameBg[2] = bg.blueF();
	uniforms.frameBg[3] = bg.alphaF();

	const auto shadowHeight = st.shadowHeight * _factor;
	const auto shadowAlpha = Viewport::kShadowMaxAlpha / 255.f;
	uniforms.shadow[0] = shadowHeight;
	uniforms.shadow[1] = shown;
	uniforms.shadow[2] = shadowAlpha;
	uniforms.shadow[3] = fullscreen ? 0.f : kBlurOpacity;

	uniforms.paused = float(paused);

	uniforms.roundRect[0] = rect.x();
	uniforms.roundRect[1] = rect.y();
	uniforms.roundRect[2] = rect.width();
	uniforms.roundRect[3] = rect.height();

	const auto radius = _owner->videoStream()
		? st::storiesRadius
		: st::roundRadiusLarge;
	uniforms.radiusOutline[0] = float(
		radius * _factor * (fullscreen ? 0. : 1.));
	uniforms.radiusOutline[1] = (outline > 0)
		? float(st::groupCallOutline * _factor)
		: 0.f;

	const auto roundBg = rhiClearColor();
	uniforms.roundBg[0] = roundBg.redF();
	uniforms.roundBg[1] = roundBg.greenF();
	uniforms.roundBg[2] = roundBg.blueF();
	uniforms.roundBg[3] = roundBg.alphaF();

	uniforms.outlineFg[0] = st::groupCallMemberActiveIcon->c.redF();
	uniforms.outlineFg[1] = st::groupCallMemberActiveIcon->c.greenF();
	uniforms.outlineFg[2] = st::groupCallMemberActiveIcon->c.blueF();
	uniforms.outlineFg[3] = st::groupCallMemberActiveIcon->c.alphaF()
		* outline;

	const auto slot = _nextOnscreenSlot++;
	if (slot >= kMaxOnscreenDraws) {
		return;
	}
	const auto vOffset = slot * kOnscreenVertexSlot;
	const auto uOffset = slot * kUniformSlot;

	_rub->updateDynamicBuffer(
		_onscreenVertexBuffer, vOffset, sizeof(frameCoords), frameCoords);
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(uniforms), &uniforms);

	auto *srb = _rhi->newShaderResourceBindings();
	_perDrawSrbs.push_back(srb);

	auto *sTex = _rgbaFrame
		? tileData.rgbaTexture
		: tileData.convertedTexture;
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			uOffset,
			sizeof(GroupFrameUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			sTex ? sTex : _placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			tileData.blurVTexture
				? tileData.blurVTexture
				: _placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_noiseTexture ? _noiseTexture : _placeholderTexture,
			_noiseRepeatSampler),
	});
	srb->create();

	_onscreenDraws.push_back({ _framePipeline, srb, vOffset });
}

void Viewport::RendererRhi::drawControls(
		not_null<VideoTile*> tile,
		TileData &tileData) {
	const auto geometry = tile->geometry().translated(
		_owner->borrowedOrigin());
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto &st = st::groupCallVideoTile;
	const auto shown = _owner->_controlsShownRatio;
	const auto fullNameShift = st.namePosition.y() + st::normalFont->height;
	const auto nameShift = anim::interpolate(fullNameShift, 0, shown);
	const auto row = tile->row();
	const auto paused = tileData.paused.value(
		tileData.pause ? 1. : 0.);

	const auto nameTop = y + (height
		- st.namePosition.y()
		- st::semiboldFont->height);
	const auto pinVisible = _owner->wide()
		&& (tile->pinInner().translated(x, y).bottom() > y);
	const auto nameVisible = (nameShift != fullNameShift);
	const auto pausedVisible = (paused > 0.);

	if (!nameVisible && !pinVisible && !pausedVisible) {
		return;
	}

	ensureButtonsImage();
	row->lazyInitialize(st::groupCallMembersListItem);

	auto drawRasterOverlay = [&](
			QRect rect,
			Fn<void(Painter&)> method,
			float opacity) {
		paintUsingRaster(rect, std::move(method), opacity);
	};

	if (pausedVisible) {
		const auto middle = (st::groupCallVideoPlaceholderHeight
			- st::groupCallPaused.height()) / 2;
		const auto pausedSpace = (nameTop - y)
			- st::groupCallPaused.height()
			- st::semiboldFont->height;
		const auto pauseIconSkip = middle
			- st::groupCallVideoPlaceholderIconTop;
		const auto pauseTextSkip = st::groupCallVideoPlaceholderTextTop
			- st::groupCallVideoPlaceholderIconTop;
		const auto pauseIconTop = !_owner->wide()
			? (y + (height - st::groupCallPaused.height()) / 2)
			: (pausedSpace < 3 * st::semiboldFont->height)
			? (pausedSpace / 3)
			: std::min(
				y + (height / 2) - pauseIconSkip,
				(nameTop
					- st::semiboldFont->height * 3
					- st::groupCallPaused.height()));

		const auto iconRect = QRect(
			x + (width - st::groupCallPaused.width()) / 2,
			pauseIconTop,
			st::groupCallPaused.width(),
			st::groupCallPaused.height());
		drawRasterOverlay(iconRect, [&](Painter &p) {
			st::groupCallPaused.paint(
				p,
				iconRect.x(),
				iconRect.y(),
				width);
		}, float(paused));

		if (_owner->wide()) {
			const auto pauseTextTop =
				(pausedSpace < 3 * st::semiboldFont->height)
				? (nameTop - (pausedSpace / 3) - st::semiboldFont->height)
				: std::min(
					pauseIconTop + pauseTextSkip,
					nameTop - st::semiboldFont->height * 2);
			const auto pausedText =
				tr::lng_group_call_video_paused(tr::now);
			const auto textWidth =
				st::semiboldFont->width(pausedText);
			const auto textRect = QRect(
				x + (width - textWidth) / 2,
				pauseTextTop,
				textWidth,
				st::semiboldFont->height);
			drawRasterOverlay(textRect, [&](Painter &p) {
				p.setPen(st::groupCallVideoTextFg);
				p.setFont(st::semiboldFont);
				p.drawText(
					textRect,
					pausedText,
					style::al_top);
			}, float(paused));
		}
	}

	if (pinVisible) {
		const auto pinInner = tile->pinInner();
		const auto pinRect = pinInner.translated(x, y);
		drawRasterOverlay(pinRect, [&](Painter &p) {
			auto hq = PainterHighQualityEnabler(p);
			VideoTile::PaintPinButton(
				p,
				tile->pinned(),
				pinRect.x(),
				pinRect.y(),
				_owner->widget()->width(),
				&_pinBackground,
				&_pinIcon);
		}, 1.f);

		const auto backInner = tile->backInner();
		const auto backRect = backInner.translated(x, y);
		drawRasterOverlay(backRect, [&](Painter &p) {
			auto hq = PainterHighQualityEnabler(p);
			VideoTile::PaintBackButton(
				p,
				backRect.x(),
				backRect.y(),
				_owner->widget()->width(),
				&_pinBackground);
		}, 1.f);
	}

	if (nameVisible) {
		const auto &icon = st::groupCallVideoCrossLine.icon;
		const auto iconLeft = x + width
			- st.iconPosition.x() - icon.width();
		const auto iconTop = y + (height
			- st.iconPosition.y()
			- icon.height()
			+ nameShift);
		const auto muteRect = QRect(
			iconLeft,
			iconTop,
			icon.width(),
			icon.height());
		if (!muteRect.isEmpty()) {
			drawRasterOverlay(muteRect, [&](Painter &p) {
				row->paintMuteIcon(
					p,
					muteRect,
					MembersRowStyle::Video);
			}, 1.f);
		}

		const auto hasWidth = width
			- st.iconPosition.x() - icon.width()
			- st.namePosition.x();
		const auto nameLeft = x + st.namePosition.x();
		const auto nameRect = QRect(
			nameLeft,
			nameTop + nameShift,
			hasWidth,
			st::semiboldFont->height);
		if (!nameRect.isEmpty()) {
			drawRasterOverlay(nameRect, [&](Painter &p) {
				p.setPen(st::groupCallVideoTextFg);
				row->name().drawLeftElided(
					p,
					nameRect.x(),
					nameRect.y(),
					nameRect.width(),
					nameRect.x() + nameRect.width());
			}, 1.f);
		}
	}
}

void Viewport::RendererRhi::paintUsingRaster(
		QRect rect,
		Fn<void(Painter&)> method,
		float opacity) {
	if (!_controlsPipeline || rect.isEmpty()) {
		return;
	}

	const auto size = rect.size() * _ifactor;
	auto raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
	raster.setDevicePixelRatio(_factor);
	raster.fill(Qt::transparent);
	{
		auto painter = Painter(&raster);
		painter.translate(-rect.topLeft());
		method(painter);
	}

	const auto slot = _nextOnscreenSlot++;
	if (slot >= kMaxOnscreenDraws) {
		return;
	}
	const auto vOffset = slot * kOnscreenVertexSlot;
	const auto uOffset = slot * kUniformSlot;

	auto *tex = acquirePoolTexture(size);
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(raster))));

	ImageUniforms imgUniforms{};
	const auto pw = float(_rt->pixelSize().width());
	const auto ph = float(_rt->pixelSize().height());
	imgUniforms.viewport[0] = pw;
	imgUniforms.viewport[1] = ph;
	imgUniforms.g_opacity = opacity;

	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(imgUniforms), &imgUniforms);

	const auto rRect = transformRect(rect);
	const float coords[] = {
		rRect.left(), rRect.bottom(),
		0.f, 0.f,

		rRect.right(), rRect.bottom(),
		1.f, 0.f,

		rRect.left(), rRect.top(),
		0.f, 1.f,

		rRect.right(), rRect.top(),
		1.f, 1.f,
	};
	_rub->updateDynamicBuffer(
		_onscreenVertexBuffer, vOffset, sizeof(coords), coords);

	auto *srb = _rhi->newShaderResourceBindings();
	_perDrawSrbs.push_back(srb);
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			uOffset,
			sizeof(ImageUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			tex,
			_linearSampler),
	});
	srb->create();

	_onscreenDraws.push_back({ _controlsPipeline, srb, vOffset });
}

Rect Viewport::RendererRhi::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

Rect Viewport::RendererRhi::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

bool Viewport::RendererRhi::isExpanded(
		not_null<VideoTile*> tile,
		QSize unscaled,
		QSize tileSize) const {
	return !tile->screencast()
		&& (!_owner->wide() || UseExpandForCamera(unscaled, tileSize));
}

float64 Viewport::RendererRhi::countExpandRatio(
		not_null<VideoTile*> tile,
		QSize unscaled,
		const TileAnimation &animation) const {
	const auto expandedFrom = isExpanded(tile, unscaled, animation.from);
	const auto expandedTo = isExpanded(tile, unscaled, animation.to);
	return (expandedFrom && expandedTo)
		? 1.
		: (!expandedFrom && !expandedTo)
		? 0.
		: expandedFrom
		? (1. - animation.ratio)
		: animation.ratio;
}

void Viewport::RendererRhi::ensureButtonsImage() {
	if (_buttons) {
		return;
	}
	const auto pinOnSize = VideoTile::PinInnerSize(true);
	const auto pinOffSize = VideoTile::PinInnerSize(false);
	const auto backSize = VideoTile::BackInnerSize();
	const auto muteSize = st::groupCallVideoCrossLine.icon.size();
	const auto pausedSize = st::groupCallPaused.size();

	const auto fullSize = QSize(
		std::max({
			pinOnSize.width(),
			pinOffSize.width(),
			backSize.width(),
			2 * muteSize.width(),
			pausedSize.width(),
		}),
		(pinOnSize.height()
			+ pinOffSize.height()
			+ backSize.height()
			+ muteSize.height()
			+ pausedSize.height()));
	const auto imageSize = fullSize * _ifactor;
	auto image = _buttons.takeImage();
	if (image.size() != imageSize) {
		image = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
	}
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		_pinOn = QRect(QPoint(), pinOnSize * _ifactor);
		VideoTile::PaintPinButton(
			p, true, 0, 0, fullSize.width(),
			&_pinBackground, &_pinIcon);

		const auto pinOffTop = pinOnSize.height();
		_pinOff = QRect(
			QPoint(0, pinOffTop) * _ifactor,
			pinOffSize * _ifactor);
		VideoTile::PaintPinButton(
			p, false, 0, pinOnSize.height(), fullSize.width(),
			&_pinBackground, &_pinIcon);

		const auto backTop = pinOffTop + pinOffSize.height();
		_back = QRect(QPoint(0, backTop) * _ifactor, backSize * _ifactor);
		VideoTile::PaintBackButton(
			p, 0, backTop, fullSize.width(), &_pinBackground);

		const auto muteTop = backTop + backSize.height();
		_muteOn = QRect(
			QPoint(0, muteTop) * _ifactor, muteSize * _ifactor);
		_muteIcon.paint(p, { 0, muteTop }, 1.);

		_muteOff = QRect(
			QPoint(muteSize.width(), muteTop) * _ifactor,
			muteSize * _ifactor);
		_muteIcon.paint(p, { muteSize.width(), muteTop }, 0.);

		const auto pausedTop = muteTop + muteSize.height();
		_pausedIcon = QRect(
			QPoint(0, pausedTop) * _ifactor,
			pausedSize * _ifactor);
		st::groupCallPaused.paint(p, 0, pausedTop, fullSize.width());
	}
	_buttons.setImage(std::move(image));
}

void Viewport::RendererRhi::validateDatas() {
	const auto &tiles = _owner->_tiles;
	const auto count = int(tiles.size());

	for (auto &data : _tileData) {
		data.stale = true;
	}
	_tileDataIndices.resize(count);
	auto pending = std::vector<int>();
	for (auto i = 0; i != count; ++i) {
		tiles[i]->row()->lazyInitialize(st::groupCallMembersListItem);
		const auto id = quintptr(tiles[i]->track().get());
		const auto j = ranges::find(_tileData, id, &TileData::id);
		if (j != end(_tileData)) {
			j->stale = false;
			_tileDataIndices[i] = int(j - begin(_tileData));
			const auto peer = tiles[i]->peer();
			if (j->peer != peer
				|| j->nameVersion != peer->nameVersion()) {
				j->peer = peer;
				j->nameVersion = peer->nameVersion();
			}
		} else {
			_tileDataIndices[i] = -1;
			pending.push_back(i);
		}
	}
	if (pending.empty()) {
		return;
	}
	auto maybeStaleAfter = begin(_tileData);
	const auto maybeStaleEnd = end(_tileData);
	for (const auto i : pending) {
		const auto id = quintptr(tiles[i]->track().get());
		const auto peer = tiles[i]->peer();
		const auto paused = (tiles[i]->track()->state()
			== Webrtc::VideoState::Paused);
		maybeStaleAfter = ranges::find(
			maybeStaleAfter,
			maybeStaleEnd,
			true,
			&TileData::stale);
		if (maybeStaleAfter != maybeStaleEnd) {
			maybeStaleAfter->id = id;
			maybeStaleAfter->peer = peer;
			maybeStaleAfter->nameVersion = peer->nameVersion();
			maybeStaleAfter->stale = false;
			maybeStaleAfter->pause = paused;
			maybeStaleAfter->paused.stop();
			maybeStaleAfter->outline = false;
			maybeStaleAfter->outlined.stop();
			maybeStaleAfter->userpicFrame = QImage();
			maybeStaleAfter->trackIndex = -1;
			_tileDataIndices[i] = int(
				maybeStaleAfter - begin(_tileData));
		} else {
			_tileDataIndices[i] = int(_tileData.size());
			_tileData.push_back({
				.id = id,
				.peer = peer,
				.nameVersion = peer->nameVersion(),
				.pause = paused,
			});
		}
	}
}

void Viewport::RendererRhi::validateOutlineAnimation(
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

void Viewport::RendererRhi::validatePausedAnimation(
		not_null<VideoTile*> tile,
		TileData &data) {
	const auto paused = (_userpicFrame
		&& tile->track()->frameSize().isEmpty())
		|| (tile->track()->state() == Webrtc::VideoState::Paused);
	if (data.pause == paused) {
		return;
	}
	data.pause = paused;
	data.paused.start(
		[=] { _owner->widget()->update(); },
		paused ? 0. : 1.,
		paused ? 1. : 0.,
		st::fadeWrapDuration);
}

void Viewport::RendererRhi::validateUserpicFrame(
		not_null<VideoTile*> tile,
		TileData &data) {
	if (!_userpicFrame) {
		data.userpicFrame = QImage();
		return;
	} else if (!data.userpicFrame.isNull()) {
		return;
	}
	const auto size = tile->trackOrUserpicSize();
	data.userpicFrame = PeerData::GenerateUserpicImage(
		tile->peer(),
		tile->row()->ensureUserpicView(),
		size.width(),
		0);
}

} // namespace Calls::Group

#endif // Qt >= 6.7
