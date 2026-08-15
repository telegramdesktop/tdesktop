/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_rhi.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/rhi/rhi_shader.h"
#include "ui/painter.h"
#include "data/data_peer_values.h"
#include "media/streaming/media_streaming_common.h"
#include "media/view/media_view_video_stream.h"
#include "platform/platform_overlay_widget.h"
#include "base/debug_log.h"
#include "styles/style_media_view.h"

#include <rhi/qrhi.h>

namespace Media::View {
namespace {

using namespace Ui::GL;

struct ImageUniforms {
	float viewport[2];
	float g_opacity;
	float _pad0;
};
static_assert(sizeof(ImageUniforms) % 16 == 0);

struct ContentUniforms {
	float viewport[2];
	float _pad0[2];
	float shadowTopRect[4];
	float shadowBottomSkipOpacityFullFade[4];
	float roundRect[4];
	float roundRadius;
	float _pad1[3];
};
static_assert(sizeof(ContentUniforms) == 80);

struct TransparentContentUniforms {
	float viewport[2];
	float _pad0[2];
	float shadowTopRect[4];
	float shadowBottomSkipOpacityFullFade[4];
	float transparentBg[4];
	float transparentFg[4];
	float transparentSize;
	float _pad1[3];
};
static_assert(sizeof(TransparentContentUniforms) == 96);

struct RoundedCornersUniforms {
	float viewport[2];
	float _pad0[2];
	float roundRect[4];
	float roundRadius;
	float _pad1[3];
};
static_assert(sizeof(RoundedCornersUniforms) == 48);

[[nodiscard]] QRectF StoryCropTextureRect(
		QSizeF imageSize,
		QSizeF targetSize) {
	if (imageSize.isEmpty() || targetSize.isEmpty()) {
		return QRectF(0., 0., 1., 1.);
	}
	const auto targetAspect = targetSize.width() / targetSize.height();
	const auto imageAspect = imageSize.width() / imageSize.height();
	if (imageAspect > targetAspect) {
		const auto cropW = imageSize.height() * targetAspect;
		const auto offset = (imageSize.width() - cropW) / 2.;
		return QRectF(
			offset / imageSize.width(),
			0.,
			cropW / imageSize.width(),
			1.);
	} else if (imageAspect < targetAspect) {
		const auto cropH = imageSize.width() / targetAspect;
		const auto offset = (imageSize.height() - cropH) / 2.;
		return QRectF(
			0.,
			offset / imageSize.height(),
			1.,
			cropH / imageSize.height());
	}
	return QRectF(0., 0., 1., 1.);
}

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Ui::Rhi::ShaderFromFile(
		u":/shaders/"_q + name + u".qsb"_q);
}

} // namespace

OverlayWidget::RendererRhi::~RendererRhi() {
	releaseResources();
}

OverlayWidget::RendererRhi::RendererRhi(not_null<OverlayWidget*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		ranges::fill(_cacheKeys, quint64(0));
		invalidateControls();
	}, _lifetime);

	crl::on_main(this, [=] {
		_owner->_storiesChanged.events(
		) | rpl::on_next([=] {
			if (_owner->_storiesSession) {
				Data::AmPremiumValue(
					_owner->_storiesSession
				) | rpl::on_next([=] {
					ranges::fill(_cacheKeys, quint64(0));
					invalidateControls();
				}, _storiesLifetime);
			} else {
				_storiesLifetime.destroy();
				ranges::fill(_cacheKeys, quint64(0));
				invalidateControls();
			}
		}, _lifetime);
	});
}

void OverlayWidget::RendererRhi::initialize(
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

	_vertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		kMaxDraws * kVertexSize);
	_vertexBuffer->create();

	_fillVertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		4 * 4 * 2 * sizeof(float));
	_fillVertexBuffer->create();

	_uniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		kMaxDraws * 256);
	_uniformBuffer->create();

	_sampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_sampler->create();

	_placeholderTexture = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
	_placeholderTexture->create();

	createPipelines();
	_initialized = true;

	LOG(("[RENDERER_TEST] component=overlay backend=%1 device=%2 status=OK")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

void OverlayWidget::RendererRhi::createPipelines() {
	const auto rpDesc = _rt->renderPassDescriptor();

	const auto argb32Vert = LoadShader(u"argb32.vert"_q);
	const auto argb32Frag = LoadShader(u"argb32.frag"_q);
	const auto controlsFrag = LoadShader(u"controls.frag"_q);

	auto *sampleSrb = _rhi->newShaderResourceBindings();
	sampleSrb->setBindings({
		QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			sizeof(ImageUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	sampleSrb->create();
	_perDrawSrbs.push_back(sampleSrb);

	auto *imagePipeline = _rhi->newGraphicsPipeline();
	imagePipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, argb32Frag },
	});
	QRhiVertexInputLayout inputLayout;
	inputLayout.setBindings({ { 4 * sizeof(float) } });
	inputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});
	imagePipeline->setVertexInputLayout(inputLayout);
	imagePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	imagePipeline->setShaderResourceBindings(sampleSrb);
	imagePipeline->setRenderPassDescriptor(rpDesc);
	imagePipeline->create();
	_imagePipeline = imagePipeline;

	auto *imageBlendPipeline = _rhi->newGraphicsPipeline();
	imageBlendPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, controlsFrag },
	});
	imageBlendPipeline->setVertexInputLayout(inputLayout);
	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	imageBlendPipeline->setTargetBlends({ blend });
	imageBlendPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	imageBlendPipeline->setShaderResourceBindings(sampleSrb);
	imageBlendPipeline->setRenderPassDescriptor(rpDesc);
	imageBlendPipeline->create();
	_imageBlendPipeline = imageBlendPipeline;

	const auto staticContentFrag = LoadShader(u"static_content.frag"_q);
	const auto transparentContentFrag = LoadShader(
		u"transparent_content.frag"_q);

	auto *contentSrb = _rhi->newShaderResourceBindings();
	contentSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	contentSrb->create();
	_perDrawSrbs.push_back(contentSrb);

	auto *staticPipeline = _rhi->newGraphicsPipeline();
	staticPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, staticContentFrag },
	});
	staticPipeline->setVertexInputLayout(inputLayout);
	staticPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	staticPipeline->setShaderResourceBindings(contentSrb);
	staticPipeline->setRenderPassDescriptor(rpDesc);
	staticPipeline->create();
	_staticContentPipeline = staticPipeline;

	auto *staticBlendPipeline = _rhi->newGraphicsPipeline();
	staticBlendPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, staticContentFrag },
	});
	staticBlendPipeline->setVertexInputLayout(inputLayout);
	staticBlendPipeline->setTargetBlends({ blend });
	staticBlendPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	staticBlendPipeline->setShaderResourceBindings(contentSrb);
	staticBlendPipeline->setRenderPassDescriptor(rpDesc);
	staticBlendPipeline->create();
	_staticContentBlendPipeline = staticBlendPipeline;

	auto *transparentSrb = _rhi->newShaderResourceBindings();
	transparentSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(TransparentContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	transparentSrb->create();
	_perDrawSrbs.push_back(transparentSrb);

	auto *transparentPipeline = _rhi->newGraphicsPipeline();
	transparentPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, transparentContentFrag },
	});
	transparentPipeline->setVertexInputLayout(inputLayout);
	transparentPipeline->setTargetBlends({ blend });
	transparentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	transparentPipeline->setShaderResourceBindings(transparentSrb);
	transparentPipeline->setRenderPassDescriptor(rpDesc);
	transparentPipeline->create();
	_transparentContentPipeline = transparentPipeline;

	const auto fillVert = LoadShader(u"fill.vert"_q);
	const auto roundedCornersFrag = LoadShader(u"rounded_corners.frag"_q);

	auto *roundedSrb = _rhi->newShaderResourceBindings();
	roundedSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(RoundedCornersUniforms)),
	});
	roundedSrb->create();
	_perDrawSrbs.push_back(roundedSrb);

	QRhiVertexInputLayout fillInputLayout;
	fillInputLayout.setBindings({ { 2 * sizeof(float) } });
	fillInputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
	});

	QRhiGraphicsPipeline::TargetBlend cornerBlend;
	cornerBlend.enable = true;
	cornerBlend.srcColor = QRhiGraphicsPipeline::Zero;
	cornerBlend.dstColor = QRhiGraphicsPipeline::SrcAlpha;
	cornerBlend.srcAlpha = QRhiGraphicsPipeline::Zero;
	cornerBlend.dstAlpha = QRhiGraphicsPipeline::SrcAlpha;

	auto *roundedPipeline = _rhi->newGraphicsPipeline();
	roundedPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, fillVert },
		{ QRhiShaderStage::Fragment, roundedCornersFrag },
	});
	roundedPipeline->setVertexInputLayout(fillInputLayout);
	roundedPipeline->setTargetBlends({ cornerBlend });
	roundedPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	roundedPipeline->setShaderResourceBindings(roundedSrb);
	roundedPipeline->setRenderPassDescriptor(rpDesc);
	roundedPipeline->create();
	_roundedCornersPipeline = roundedPipeline;

	const auto yuv420Frag = LoadShader(u"yuv420_content.frag"_q);
	const auto nv12Frag = LoadShader(u"nv12_content.frag"_q);

	auto *yuv420Srb = _rhi->newShaderResourceBindings();
	yuv420Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			4,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	yuv420Srb->create();
	_perDrawSrbs.push_back(yuv420Srb);

	auto createYuvPipeline = [&](const QShader &frag, bool blending,
			QRhiShaderResourceBindings *srb) {
		auto *pipeline = _rhi->newGraphicsPipeline();
		pipeline->setShaderStages({
			{ QRhiShaderStage::Vertex, argb32Vert },
			{ QRhiShaderStage::Fragment, frag },
		});
		pipeline->setVertexInputLayout(inputLayout);
		if (blending) {
			pipeline->setTargetBlends({ blend });
		}
		pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
		pipeline->setShaderResourceBindings(srb);
		pipeline->setRenderPassDescriptor(rpDesc);
		pipeline->create();
		return pipeline;
	};

	_yuv420Pipeline = createYuvPipeline(yuv420Frag, false, yuv420Srb);
	_yuv420BlendPipeline = createYuvPipeline(yuv420Frag, true, yuv420Srb);

	auto *nv12Srb = _rhi->newShaderResourceBindings();
	nv12Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	nv12Srb->create();
	_perDrawSrbs.push_back(nv12Srb);

	_nv12Pipeline = createYuvPipeline(nv12Frag, false, nv12Srb);
	_nv12BlendPipeline = createYuvPipeline(nv12Frag, true, nv12Srb);
}

void OverlayWidget::RendererRhi::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_owner->_hideWorkaround) {
		return;
	}
	_rhi = rhi;
	_rt = rt;
	_cb = cb;
	_nextVertexSlot = 0;
	_nextPoolIndex = 0;
	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();
	_drawCommands.clear();

	const auto size = rt->pixelSize();
	const auto factor = _owner->widget()->devicePixelRatioF();
	if (_factor != factor) {
		_factor = factor;
		_ifactor = int(std::ceil(factor));
		ranges::fill(_cacheKeys, quint64(0));
		invalidateControls();
		delete _controlsFadeTexture;
		_controlsFadeTexture = nullptr;
	}
	_viewport = QSize(
		int(size.width() / _factor),
		int(size.height() / _factor));

	_rub = rhi->nextResourceUpdateBatch();
	_pendingVideoStream = nullptr;

	_owner->paint(this);

	if (_pendingVideoStream) {
		_pendingVideoStream->borrowedPaintOffscreen(_rhi, _rt, _cb);
	}

	if (const auto notch = _owner->topNotchSkip()) {
		auto blackImage = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
		blackImage.fill(Qt::black);
		auto *blackTex = acquirePoolTexture(QSize(1, 1));
		_rub->uploadTexture(
			blackTex,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(blackImage))));
		const auto notchRect = transformRect(
			QRect(0, 0, _owner->width(), notch));
		const float notchCoords[] = {
			notchRect.left(), notchRect.bottom(), 0.f, 0.f,
			notchRect.right(), notchRect.bottom(), 1.f, 0.f,
			notchRect.left(), notchRect.top(), 0.f, 1.f,
			notchRect.right(), notchRect.top(), 1.f, 1.f,
		};
		drawTexturedQuad(_imagePipeline, blackTex, notchCoords);
	}

	cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, _rub);
	_rub = nullptr;

	for (const auto &cmd : _drawCommands) {
		cb->setGraphicsPipeline(cmd.pipeline);
		cb->setShaderResources(cmd.srb);
		cb->setViewport({
			0, 0,
			float(rt->pixelSize().width()),
			float(rt->pixelSize().height()) });
		if (cmd.fillVertex) {
			const QRhiCommandBuffer::VertexInput vbufBinding(
				_fillVertexBuffer,
				cmd.vertexIndex * 4 * 2 * sizeof(float));
			cb->setVertexInput(0, 1, &vbufBinding);
		} else {
			const QRhiCommandBuffer::VertexInput vbufBinding(
				_vertexBuffer, cmd.vertexIndex * kVertexSize);
			cb->setVertexInput(0, 1, &vbufBinding);
		}
		cb->draw(4);
	}
	_drawCommands.clear();

	if (_pendingVideoStream) {
		_pendingVideoStream->borrowedPaintOnscreen(_rhi, _rt, _cb);
		_pendingVideoStream = nullptr;
	}

	cb->endPass();
}

QRhiTexture *OverlayWidget::RendererRhi::acquirePoolTexture(QSize size) {
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

void OverlayWidget::RendererRhi::releaseResources() {
	_drawCommands.clear();

	for (auto &entry : _texturePool) {
		delete entry.texture;
	}
	_texturePool.clear();

	delete _imagePipeline;
	_imagePipeline = nullptr;
	delete _imageBlendPipeline;
	_imageBlendPipeline = nullptr;
	delete _staticContentPipeline;
	_staticContentPipeline = nullptr;
	delete _staticContentBlendPipeline;
	_staticContentBlendPipeline = nullptr;
	delete _transparentContentPipeline;
	_transparentContentPipeline = nullptr;
	delete _roundedCornersPipeline;
	_roundedCornersPipeline = nullptr;
	delete _yuv420Pipeline;
	_yuv420Pipeline = nullptr;
	delete _yuv420BlendPipeline;
	_yuv420BlendPipeline = nullptr;
	delete _nv12Pipeline;
	_nv12Pipeline = nullptr;
	delete _nv12BlendPipeline;
	_nv12BlendPipeline = nullptr;

	for (auto &tex : _storiesSiblingTextures) {
		delete tex;
		tex = nullptr;
	}
	ranges::fill(_storiesSiblingSizes, QSize());
	ranges::fill(_storiesSiblingCacheKeys, quint64(0));

	delete _controlsAtlasTexture;
	_controlsAtlasTexture = nullptr;
	_controlsAtlasSize = QSize();
	ranges::fill(_controlsTextures, QRect());

	delete _controlsFadeTexture;
	_controlsFadeTexture = nullptr;
	_controlsFadeSize = QSize();

	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();

	for (auto &tex : _rgbaTextures) {
		delete tex;
		tex = nullptr;
	}
	ranges::fill(_rgbaSizes, QSize());
	ranges::fill(_cacheKeys, quint64(0));

	delete _yTexture;
	_yTexture = nullptr;
	delete _uTexture;
	_uTexture = nullptr;
	delete _vTexture;
	_vTexture = nullptr;
	delete _uvTexture;
	_uvTexture = nullptr;
	_lumaSize = QSize();
	_chromaSize = QSize();
	_chromaNV12 = false;
	_usingExternalVideoTextures = false;
	_trackFrameIndex = 0;
	_streamedIndex = 0;
#ifdef Q_OS_MAC
	_metalTextureCache.flush();
#endif // Q_OS_MAC

	delete _placeholderTexture;
	_placeholderTexture = nullptr;

	delete _vertexBuffer;
	_vertexBuffer = nullptr;
	delete _fillVertexBuffer;
	_fillVertexBuffer = nullptr;
	delete _uniformBuffer;
	_uniformBuffer = nullptr;
	delete _sampler;
	_sampler = nullptr;

	_initialized = false;
}

QColor OverlayWidget::RendererRhi::rhiClearColor() {
	if (_owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

std::optional<QColor> OverlayWidget::RendererRhi::clearColor() {
	if (_owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

void OverlayWidget::RendererRhi::drawTexturedQuad(
		QRhiGraphicsPipeline *pipeline,
		QRhiTexture *texture,
		const float *coords,
		float opacity,
		bool blend) {
	if (_nextVertexSlot >= kMaxDraws || !texture) {
		return;
	}
	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;

	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	ImageUniforms uniforms{};
	uniforms.viewport[0] = _viewport.width() * _factor;
	uniforms.viewport[1] = _viewport.height() * _factor;
	uniforms.g_opacity = opacity;
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(ImageUniforms), &uniforms);

	auto *srb = _rhi->newShaderResourceBindings();
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
			texture,
			_sampler),
	});
	srb->create();
	_perDrawSrbs.push_back(srb);

	_drawCommands.push_back({
		.pipeline = blend ? _imageBlendPipeline : pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});
}

void OverlayWidget::RendererRhi::paintUsingRaster(
		QRect rect,
		Fn<void(Painter&)> method,
		bool transparent,
		float opacity) {
	if (!_imagePipeline || rect.isEmpty()) {
		return;
	}
	const auto size = rect.size() * _ifactor;
	auto raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
	raster.setDevicePixelRatio(_factor);
	raster.fill(Qt::transparent);
	{
		auto painter = Painter(&raster);
		method(painter);
	}

	auto *tex = acquirePoolTexture(size);
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(raster))));

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

	drawTexturedQuad(
		_imagePipeline,
		tex,
		coords,
		opacity,
		transparent);
}

void OverlayWidget::RendererRhi::validateControlsFade() {
	const auto forStories = (_owner->_stories != nullptr);
	const auto flip = !forStories && !_owner->topShadowOnTheRight();
	if (_controlsFadeTexture
		&& _shadowTopFlip == flip
		&& _shadowsForStories == forStories) {
		return;
	}
	_shadowTopFlip = flip;
	_shadowsForStories = forStories;
	const auto &top = _shadowsForStories
		? st::storiesShadowTop
		: st::mediaviewShadowTop;
	const auto &bottom = _shadowsForStories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	const auto width = top.width();
	const auto bottomTop = top.height();
	const auto height = bottomTop + bottom.height();

	auto image = QImage(
		QSize(width, height) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		top.paint(p, 0, 0, width);
		bottom.fill(p, QRect(0, bottomTop, width, bottom.height()));
	}
	if (flip) {
		image = std::move(image).mirrored(true, false);
	}

	const auto size = image.size();
	if (!_controlsFadeTexture || _controlsFadeSize != size) {
		delete _controlsFadeTexture;
		_controlsFadeTexture = _rhi->newTexture(
			QRhiTexture::BGRA8,
			size);
		_controlsFadeTexture->create();
		_controlsFadeSize = size;
	}
	_rub->uploadTexture(
		_controlsFadeTexture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(image))));
}

void OverlayWidget::RendererRhi::fillShadowUniforms(
		float *shadowTopRect,
		float *shadowBottomSkipOpacityFullFade,
		ContentGeometry geometry) const {
	if (_owner->_stories) {
		const auto &top = st::storiesShadowTop.size();
		const auto shadowTop = geometry.topShadowShown
			? geometry.rect.y()
			: geometry.rect.y() - top.height();
		const auto tRect = transformRect(
			QRect(QPoint(geometry.rect.x(), shadowTop), top));
		shadowTopRect[0] = tRect.x();
		shadowTopRect[1] = tRect.y();
		shadowTopRect[2] = tRect.width();
		shadowTopRect[3] = tRect.height();
	} else {
		const auto &top = st::mediaviewShadowTop.size();
		const auto point = QPoint(
			_shadowTopFlip ? 0 : (_viewport.width() - top.width()),
			0);
		const auto tRect = transformRect(QRect(point, top));
		shadowTopRect[0] = tRect.x();
		shadowTopRect[1] = tRect.y();
		shadowTopRect[2] = tRect.width();
		shadowTopRect[3] = tRect.height();
	}
	const auto &bottom = _owner->_stories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	shadowBottomSkipOpacityFullFade[0] = bottom.height() * _factor;
	shadowBottomSkipOpacityFullFade[1] =
		geometry.bottomShadowSkip * _factor;
	shadowBottomSkipOpacityFullFade[2] = geometry.controlsOpacity;
	shadowBottomSkipOpacityFullFade[3] = 1.f - float(geometry.fade);
}

void OverlayWidget::RendererRhi::drawContentQuad(
		QRhiTexture *contentTexture,
		const float *coords,
		ContentGeometry geometry,
		bool fillTransparentBackground,
		bool blend) {
	if (_nextVertexSlot >= kMaxDraws || !contentTexture) {
		return;
	}

	validateControlsFade();

	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;

	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;

	auto *pipeline = fillTransparentBackground
		? _transparentContentPipeline
		: (blend ? _staticContentBlendPipeline : _staticContentPipeline);

	if (fillTransparentBackground) {
		const auto c_bg = st::mediaviewTransparentBg->c;
		const auto c_fg = st::mediaviewTransparentFg->c;
		TransparentContentUniforms uniforms{};
		uniforms.viewport[0] = vw;
		uniforms.viewport[1] = vh;
		fillShadowUniforms(
			uniforms.shadowTopRect,
			uniforms.shadowBottomSkipOpacityFullFade,
			geometry);
		uniforms.transparentBg[0] = float(c_bg.redF());
		uniforms.transparentBg[1] = float(c_bg.greenF());
		uniforms.transparentBg[2] = float(c_bg.blueF());
		uniforms.transparentBg[3] = float(c_bg.alphaF());
		uniforms.transparentFg[0] = float(c_fg.redF());
		uniforms.transparentFg[1] = float(c_fg.greenF());
		uniforms.transparentFg[2] = float(c_fg.blueF());
		uniforms.transparentFg[3] = float(c_fg.alphaF());
		uniforms.transparentSize =
			st::transparentPlaceholderSize * _factor;
		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(TransparentContentUniforms),
			&uniforms);
	} else {
		ContentUniforms uniforms{};
		uniforms.viewport[0] = vw;
		uniforms.viewport[1] = vh;
		fillShadowUniforms(
			uniforms.shadowTopRect,
			uniforms.shadowBottomSkipOpacityFullFade,
			geometry);
		const auto rRect = scaleRect(
			transformRect(geometry.rect),
			geometry.scale);
		if (geometry.roundRadius) {
			uniforms.roundRect[0] = rRect.x();
			uniforms.roundRect[1] = rRect.y();
			uniforms.roundRect[2] = rRect.width();
			uniforms.roundRect[3] = rRect.height();
		} else {
			uniforms.roundRect[0] = 0;
			uniforms.roundRect[1] = 0;
			uniforms.roundRect[2] = vw;
			uniforms.roundRect[3] = vh;
		}
		uniforms.roundRadius = geometry.roundRadius * _factor;
		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(ContentUniforms),
			&uniforms);
	}

	auto *srb = _rhi->newShaderResourceBindings();
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			uOffset,
			fillTransparentBackground
				? int(sizeof(TransparentContentUniforms))
				: int(sizeof(ContentUniforms))),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			contentTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_controlsFadeTexture
				? _controlsFadeTexture
				: _placeholderTexture,
			_sampler),
	});
	srb->create();
	_perDrawSrbs.push_back(srb);

	_drawCommands.push_back({
		.pipeline = pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});
}

void OverlayWidget::RendererRhi::paintBackground() {
	const auto &bg = _owner->_fullScreenVideo
		? st::mediaviewVideoBg
		: st::mediaviewBg;
	const auto c = bg->c;
	const auto vw = float(_viewport.width() * _factor);
	const auto vh = float(_viewport.height() * _factor);

	auto bgImage = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	bgImage.fill(c);

	auto *tex = acquirePoolTexture(QSize(1, 1));
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(bgImage))));

	const float coords[] = {
		0.f, vh, 0.f, 0.f,
		vw,  vh, 1.f, 0.f,
		0.f, 0.f, 0.f, 1.f,
		vw,  0.f, 1.f, 1.f,
	};
	drawTexturedQuad(_imagePipeline, tex, coords);
}

void OverlayWidget::RendererRhi::paintVideoStream() {
	_pendingVideoStream = _owner->_videoStream.get();
}

void OverlayWidget::RendererRhi::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	} else if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.image.isNull());
		paintTransformedStaticContent(
			data.image,
			geometry,
			data.alpha,
			data.alpha);
		return;
	}
	const auto nativeTexture =
		(data.format == Streaming::FrameFormat::NativeTexture);
	const auto nv12 = nativeTexture
		|| (data.format == Streaming::FrameFormat::NV12);
	const auto yuv = data.yuv;
	const auto nv12changed = !nativeTexture && (_chromaNV12 != nv12);

	const auto upload = (_trackFrameIndex != data.index)
		|| (_streamedIndex != _owner->streamedIndex());

	_trackFrameIndex = data.index;
	_streamedIndex = _owner->streamedIndex();

	if (upload) {
		auto zeroCopied = false;
#ifdef Q_OS_MAC
		if (nativeTexture && data.nativeFrame
			&& data.nativeFrame->pixelBuffer) {
			if (_metalTextureCache.createTexturesFromPixelBuffer(
					_rhi,
					data.nativeFrame->pixelBuffer,
					&_yTexture,
					&_uvTexture,
					&_lumaSize,
					&_chromaSize)) {
				_chromaNV12 = true;
				zeroCopied = true;
			} else {
				return;
			}
		}
#endif // Q_OS_MAC
		if (!zeroCopied) {
		if (_usingExternalVideoTextures) {
			delete _yTexture;
			_yTexture = nullptr;
			delete _uTexture;
			_uTexture = nullptr;
			delete _vTexture;
			_vTexture = nullptr;
			delete _uvTexture;
			_uvTexture = nullptr;
			_lumaSize = QSize();
			_chromaSize = QSize();
#ifdef Q_OS_MAC
			_metalTextureCache.flush();
#endif // Q_OS_MAC
		}
		if (!yuv || yuv->size.isEmpty()) {
			return;
		}
		if (!_yTexture || _lumaSize != yuv->size) {
			delete _yTexture;
			_yTexture = _rhi->newTexture(QRhiTexture::R8, yuv->size);
			_yTexture->create();
			_lumaSize = yuv->size;
		}
		auto yDesc = QRhiTextureSubresourceUploadDescription(
			yuv->y.data,
			yuv->y.stride * yuv->size.height());
		yDesc.setDataStride(yuv->y.stride);
		_rub->uploadTexture(
			_yTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, yDesc)));

		if (nv12) {
			if (!_uvTexture || nv12changed
				|| _chromaSize != yuv->chromaSize) {
				delete _uvTexture;
				_uvTexture = _rhi->newTexture(
					QRhiTexture::RG8,
					yuv->chromaSize);
				_uvTexture->create();
				_chromaSize = yuv->chromaSize;
			}
			auto uvDesc = QRhiTextureSubresourceUploadDescription(
				yuv->u.data,
				yuv->u.stride * yuv->chromaSize.height());
			uvDesc.setDataStride(yuv->u.stride);
			_rub->uploadTexture(
				_uvTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, uvDesc)));
		} else {
			if (!_uTexture || nv12changed
				|| _chromaSize != yuv->chromaSize) {
				delete _uTexture;
				_uTexture = _rhi->newTexture(
					QRhiTexture::R8,
					yuv->chromaSize);
				_uTexture->create();
				delete _vTexture;
				_vTexture = _rhi->newTexture(
					QRhiTexture::R8,
					yuv->chromaSize);
				_vTexture->create();
				_chromaSize = yuv->chromaSize;
			}
			auto uDesc = QRhiTextureSubresourceUploadDescription(
				yuv->u.data,
				yuv->u.stride * yuv->chromaSize.height());
			uDesc.setDataStride(yuv->u.stride);
			_rub->uploadTexture(
				_uTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, uDesc)));
			auto vDesc = QRhiTextureSubresourceUploadDescription(
				yuv->v.data,
				yuv->v.stride * yuv->chromaSize.height());
			vDesc.setDataStride(yuv->v.stride);
			_rub->uploadTexture(
				_vTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, vDesc)));
		}
		_chromaNV12 = nv12;
		} // if (!zeroCopied)
		_usingExternalVideoTextures = zeroCopied;
	}

	validateControlsFade();

	if (_nextVertexSlot >= kMaxDraws) {
		return;
	}

	const auto textureRect = _owner->_stories
		? StoryCropTextureRect(
			QSizeF(nativeTexture ? _lumaSize : yuv->size),
			geometry.rect.size())
		: QRectF(0., 0., 1., 1.);
	const auto texLeft = float(textureRect.x());
	const auto texRight = float(textureRect.x() + textureRect.width());
	const auto texTop = float(textureRect.y());
	const auto texBottom = float(textureRect.y() + textureRect.height());

	const auto rRect = scaleRect(
		transformRect(geometry.rect),
		geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());

	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;
	const float coords[] = {
		tl[0], tl[1], texLeft, texTop,
		tr[0], tr[1], texRight, texTop,
		bl[0], bl[1], texLeft, texBottom,
		br[0], br[1], texRight, texBottom,
	};
	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;
	ContentUniforms uniforms{};
	uniforms.viewport[0] = vw;
	uniforms.viewport[1] = vh;
	fillShadowUniforms(
		uniforms.shadowTopRect,
		uniforms.shadowBottomSkipOpacityFullFade,
		geometry);
	if (geometry.roundRadius) {
		uniforms.roundRect[0] = rRect.x();
		uniforms.roundRect[1] = rRect.y();
		uniforms.roundRect[2] = rRect.width();
		uniforms.roundRect[3] = rRect.height();
	} else {
		uniforms.roundRect[0] = 0;
		uniforms.roundRect[1] = 0;
		uniforms.roundRect[2] = vw;
		uniforms.roundRect[3] = vh;
	}
	uniforms.roundRadius = geometry.roundRadius * _factor;
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(ContentUniforms), &uniforms);

	const auto blendEnabled = (geometry.roundRadius > 0.);
	auto *srb = _rhi->newShaderResourceBindings();
	if (nv12) {
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(ContentUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_yTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2,
				QRhiShaderResourceBinding::FragmentStage,
				_uvTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3,
				QRhiShaderResourceBinding::FragmentStage,
				_controlsFadeTexture
					? _controlsFadeTexture
					: _placeholderTexture,
				_sampler),
		});
	} else {
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(ContentUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_yTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2,
				QRhiShaderResourceBinding::FragmentStage,
				_uTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3,
				QRhiShaderResourceBinding::FragmentStage,
				_vTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				4,
				QRhiShaderResourceBinding::FragmentStage,
				_controlsFadeTexture
					? _controlsFadeTexture
					: _placeholderTexture,
				_sampler),
		});
	}
	srb->create();
	_perDrawSrbs.push_back(srb);

	auto *pipeline = nv12
		? (blendEnabled ? _nv12BlendPipeline : _nv12Pipeline)
		: (blendEnabled ? _yuv420BlendPipeline : _yuv420Pipeline);
	_drawCommands.push_back({
		.pipeline = pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});

	if (_owner->_recognitionResult.success
		&& !_owner->_recognitionResult.items.empty()) {
		const auto opacity = _owner->_recognitionAnimation.value(
			_owner->_showRecognitionResults ? 1. : 0.);
		if (opacity > 0.) {
			paintRecognitionOverlay(data.image, geometry);
		}
	}
}

void OverlayWidget::RendererRhi::paintRecognitionOverlay(
		const QImage &image,
		ContentGeometry geometry) {
	if (!_owner->_recognitionResult.success
		|| _owner->_recognitionResult.items.empty()) {
		return;
	}
	const auto opacity = _owner->_recognitionAnimation.value(
		_owner->_showRecognitionResults ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	const auto rect = geometry.rect;
	const auto overlaySize = rect.size().toSize() * _ifactor;
	if (overlaySize.isEmpty()) {
		return;
	}
	auto overlay = QImage(
		overlaySize,
		QImage::Format_ARGB32_Premultiplied);
	overlay.setDevicePixelRatio(_factor);
	overlay.fill(QColor(0, 0, 0, int(77 * opacity)));

	const auto scale = rect.width() / float(image.width());
	{
		auto p = QPainter(&overlay);
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		for (const auto &item : _owner->_recognitionResult.items) {
			const auto &r = item.rect;
			p.fillRect(QRectF(
				r.x() * _ifactor * scale,
				r.y() * _ifactor * scale,
				r.width() * _ifactor * scale,
				r.height() * _ifactor * scale), Qt::transparent);
		}
		const auto spans = _owner->_recognition.spans();
		if (!spans.empty()) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			const auto color = QColor(48, 128, 255, int(128 * opacity));
			for (const auto &span : spans) {
				const auto band = _owner->_recognition.bandFor(
					span.item,
					span.from,
					span.till);
				if (!band.isEmpty()) {
					p.fillRect(QRectF(
						band.x() * _ifactor * scale,
						band.y() * _ifactor * scale,
						band.width() * _ifactor * scale,
						band.height() * _ifactor * scale), color);
				}
			}
		}
	}

	auto *tex = acquirePoolTexture(overlaySize);
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(overlay))));

	const auto rRect = scaleRect(transformRect(rect), geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());
	const float coords[] = {
		tl[0], tl[1], 0.f, 0.f,
		tr[0], tr[1], 1.f, 0.f,
		bl[0], bl[1], 0.f, 1.f,
		br[0], br[1], 1.f, 1.f,
	};
	drawTexturedQuad(_imagePipeline, tex, coords, 1.f, true);
}

void OverlayWidget::RendererRhi::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground,
		int index) {
	Expects(index >= 0 && index < 3);

	if (image.isNull() || geometry.rect.isEmpty() || !_imagePipeline) {
		return;
	}

	const auto cacheKey = image.cacheKey();
	const auto upload = (_cacheKeys[index] != cacheKey);
	if (upload) {
		_cacheKeys[index] = cacheKey;
		if (!_rgbaTextures[index]
			|| _rgbaSizes[index] != image.size()) {
			delete _rgbaTextures[index];
			_rgbaTextures[index] = _rhi->newTexture(
				QRhiTexture::BGRA8,
				image.size());
			_rgbaTextures[index]->create();
			_rgbaSizes[index] = image.size();
		}
		_rub->uploadTexture(
			_rgbaTextures[index],
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(image))));
	}

	const auto textureRect = _owner->_stories
		? StoryCropTextureRect(
			QSizeF(image.size()),
			geometry.rect.size())
		: QRectF(0., 0., 1., 1.);
	const auto texLeft = float(textureRect.x());
	const auto texRight = float(textureRect.x() + textureRect.width());
	const auto texTop = float(textureRect.y());
	const auto texBottom = float(textureRect.y() + textureRect.height());

	const auto rRect = scaleRect(
		transformRect(geometry.rect),
		geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());
	const float coords[] = {
		tl[0], tl[1], texLeft, texTop,
		tr[0], tr[1], texRight, texTop,
		bl[0], bl[1], texLeft, texBottom,
		br[0], br[1], texRight, texBottom,
	};

	const auto blend = (geometry.roundRadius > 0.)
		|| (semiTransparent && !fillTransparentBackground);
	drawContentQuad(
		_rgbaTextures[index],
		coords,
		geometry,
		fillTransparentBackground,
		blend);
	paintRecognitionOverlay(image, geometry);
}

void OverlayWidget::RendererRhi::paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) {
	paintUsingRaster(inner, [&](Painter &p) {
		const auto newInner = QRect(QPoint(), inner.size());
		_owner->paintRadialLoadingContent(p, newInner, radial, radialOpacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintThemePreview(QRect outer) {
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintThemePreviewContent(p, newOuter, newOuter);
	});
}

void OverlayWidget::RendererRhi::paintDocumentBubble(
		QRect outer,
		QRect icon) {
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		const auto newIcon = icon.translated(-outer.topLeft());
		_owner->paintDocumentBubbleContent(p, newOuter, newIcon, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintSaveMsg(QRect outer) {
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSaveMsgContent(p, newOuter, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintChapter(QRect outer) {
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintChapterContent(p, newOuter, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintSpeedBoost(QRect outer) {
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSpeedBoostContent(p, newOuter, newOuter);
	}, true);
}

auto OverlayWidget::RendererRhi::controlMeta(Over control) const
-> Control {
	const auto stories = [&] {
		return (_owner->_stories != nullptr);
	};
	switch (control) {
	case Over::Left: return {
		0,
		stories() ? &st::storiesLeft : &st::mediaviewLeft
	};
	case Over::Right: return {
		1,
		stories() ? &st::storiesRight : &st::mediaviewRight
	};
	case Over::Save: return {
		2,
		(_owner->saveControlLocked()
			? &st::mediaviewSaveLocked
			: &st::mediaviewSave)
	};
	case Over::Share: return { 3, &st::mediaviewShare };
	case Over::Rotate: return { 4, &st::mediaviewRotate };
	case Over::More: return { 5, &st::mediaviewMore };
	case Over::Draw: return { 6, &st::mediaviewDraw };
	case Over::Recognize: return { 7, &st::mediaviewRecognize };
	}
	Unexpected("Control value in OverlayWidget::RendererRhi::controlMeta.");
}

void OverlayWidget::RendererRhi::validateControls() {
	if (_controlsAtlasTexture) {
		return;
	}
	const auto metas = {
		controlMeta(Over::Left),
		controlMeta(Over::Right),
		controlMeta(Over::Save),
		controlMeta(Over::Share),
		controlMeta(Over::Rotate),
		controlMeta(Over::More),
		controlMeta(Over::Draw),
		controlMeta(Over::Recognize),
	};
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += meta.icon->height();
	}
	maxWidth = std::max(st::mediaviewIconOver, maxWidth);
	fullHeight += st::mediaviewIconOver;
	auto image = QImage(
		QSize(maxWidth, fullHeight) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		auto index = 0;
		auto height = 0;
		for (const auto &meta : metas) {
			meta.icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _ifactor,
				meta.icon->size() * _ifactor);
			height += meta.icon->height();
		}
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(OverBackgroundColor());
		p.drawEllipse(
			QRect(0, height, st::mediaviewIconOver, st::mediaviewIconOver));
		_controlsTextures[index++] = QRect(
			QPoint(0, height) * _ifactor,
			QSize(st::mediaviewIconOver, st::mediaviewIconOver) * _ifactor);
	}

	const auto size = image.size();
	_controlsAtlasTexture = _rhi->newTexture(QRhiTexture::BGRA8, size);
	_controlsAtlasTexture->create();
	_controlsAtlasSize = size;
	_rub->uploadTexture(
		_controlsAtlasTexture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(image))));
}

void OverlayWidget::RendererRhi::invalidateControls() {
	delete _controlsAtlasTexture;
	_controlsAtlasTexture = nullptr;
	ranges::fill(_controlsTextures, QRect());
}

void OverlayWidget::RendererRhi::paintControlsStart() {
	validateControls();
}

void OverlayWidget::RendererRhi::paintControl(
		Over control,
		QRect over,
		float64 overOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	if (!_controlsAtlasTexture) {
		return;
	}
	const auto meta = controlMeta(control);
	Assert(meta.icon == &icon);

	const auto atlasW = float(_controlsAtlasSize.width());
	const auto atlasH = float(_controlsAtlasSize.height());

	if (!over.isEmpty() && overOpacity > 0.) {
		const auto overAlpha = float(overOpacity * kOverBackgroundOpacity);
		const auto &overTex = _controlsTextures[kControlsCount];
		const auto overGeometry = transformRect(over);
		// QRect::right()/bottom() return x+width-1 / y+height-1
		// (inclusive), which is one texel short of the atlas sub-rect.
		// Sampling only up to that coordinate stretches the (width-1)
		// texels across the full destination quad, making the rendered
		// icon / hover circle look one pixel too wide and clips its
		// right/bottom antialiased edge.
		const auto tl = overTex.left() / atlasW;
		const auto tr = (overTex.x() + overTex.width()) / atlasW;
		const auto tt = overTex.top() / atlasH;
		const auto tb = (overTex.y() + overTex.height()) / atlasH;
		const float overCoords[] = {
			overGeometry.left(), overGeometry.bottom(), tl, tt,
			overGeometry.right(), overGeometry.bottom(), tr, tt,
			overGeometry.left(), overGeometry.top(), tl, tb,
			overGeometry.right(), overGeometry.top(), tr, tb,
		};
		drawTexturedQuad(
			_imagePipeline,
			_controlsAtlasTexture,
			overCoords,
			overAlpha,
			true);
	}

	const auto &iconTex = _controlsTextures[meta.index];
	const auto iconGeometry = transformRect(inner);
	const auto tl = iconTex.left() / atlasW;
	const auto tr = (iconTex.x() + iconTex.width()) / atlasW;
	const auto tt = iconTex.top() / atlasH;
	const auto tb = (iconTex.y() + iconTex.height()) / atlasH;
	const float iconCoords[] = {
		iconGeometry.left(), iconGeometry.bottom(), tl, tt,
		iconGeometry.right(), iconGeometry.bottom(), tr, tt,
		iconGeometry.left(), iconGeometry.top(), tl, tb,
		iconGeometry.right(), iconGeometry.top(), tr, tb,
	};
	drawTexturedQuad(
		_imagePipeline,
		_controlsAtlasTexture,
		iconCoords,
		float(innerOpacity),
		true);
}

void OverlayWidget::RendererRhi::paintFooter(
		QRect outer,
		float64 opacity) {
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintFooterContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintCaption(
		QRect outer,
		float64 opacity) {
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintCaptionContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintGroupThumbs(
		QRect outer,
		float64 opacity) {
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintGroupThumbsContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintRoundedCorners(int radius) {
	if (!_roundedCornersPipeline || radius <= 0) {
		return;
	}

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;

	RoundedCornersUniforms uniforms{};
	uniforms.viewport[0] = vw;
	uniforms.viewport[1] = vh;
	const auto roundRect = transformRect(QRect(QPoint(), _viewport));
	uniforms.roundRect[0] = roundRect.x();
	uniforms.roundRect[1] = roundRect.y();
	uniforms.roundRect[2] = roundRect.width();
	uniforms.roundRect[3] = roundRect.height();
	uniforms.roundRadius = radius * _factor;

	const auto topLeft = transformRect(
		QRect(0, 0, radius, radius));
	const auto topRight = transformRect(
		QRect(_viewport.width() - radius, 0, radius, radius));
	const auto bottomRight = transformRect(QRect(
		_viewport.width() - radius,
		_viewport.height() - radius,
		radius,
		radius));
	const auto bottomLeft = transformRect(
		QRect(0, _viewport.height() - radius, radius, radius));

	const auto corners = {
		std::ref(topLeft),
		std::ref(topRight),
		std::ref(bottomRight),
		std::ref(bottomLeft),
	};

	auto cornerIndex = 0;
	for (const auto &corner : corners) {
		if (_nextVertexSlot >= kMaxDraws) {
			break;
		}
		const auto slot = _nextVertexSlot++;
		const auto uOffset = slot * 256;

		const float coords[] = {
			corner.get().left(), corner.get().bottom(),
			corner.get().right(), corner.get().bottom(),
			corner.get().left(), corner.get().top(),
			corner.get().right(), corner.get().top(),
		};

		_rub->updateDynamicBuffer(
			_fillVertexBuffer,
			cornerIndex * int(sizeof(coords)),
			sizeof(coords),
			coords);

		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(RoundedCornersUniforms),
			&uniforms);

		auto *srb = _rhi->newShaderResourceBindings();
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(RoundedCornersUniforms)),
		});
		srb->create();
		_perDrawSrbs.push_back(srb);

		_drawCommands.push_back({
			.pipeline = _roundedCornersPipeline,
			.srb = srb,
			.vertexIndex = cornerIndex,
			.fillVertex = true,
		});
		++cornerIndex;
	}
}

void OverlayWidget::RendererRhi::paintStoriesSiblingPart(
		int index,
		const QImage &image,
		QRect rect,
		float64 opacity) {
	Expects(index >= 0 && index < kStoriesSiblingPartsCount);

	if (image.isNull() || rect.isEmpty()) {
		return;
	}

	const auto cacheKey = image.cacheKey();
	if (_storiesSiblingCacheKeys[index] != cacheKey
		|| !_storiesSiblingTextures[index]
		|| _storiesSiblingSizes[index] != image.size()) {
		_storiesSiblingCacheKeys[index] = cacheKey;
		delete _storiesSiblingTextures[index];
		_storiesSiblingTextures[index] = _rhi->newTexture(
			QRhiTexture::BGRA8,
			image.size());
		_storiesSiblingTextures[index]->create();
		_storiesSiblingSizes[index] = image.size();
		_rub->uploadTexture(
			_storiesSiblingTextures[index],
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(image))));
	}

	const auto rRect = transformRect(rect);
	const float coords[] = {
		rRect.left(), rRect.bottom(), 0.f, 0.f,
		rRect.right(), rRect.bottom(), 1.f, 0.f,
		rRect.left(), rRect.top(), 0.f, 1.f,
		rRect.right(), rRect.top(), 1.f, 1.f,
	};
	drawTexturedQuad(
		_imagePipeline,
		_storiesSiblingTextures[index],
		coords,
		float(opacity),
		true);
}

Rect OverlayWidget::RendererRhi::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::transformRect(const QRectF &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::scaleRect(
		const Rect &unscaled,
		float64 scale) const {
	const auto added = scale - 1.;
	const auto addw = unscaled.width() * added;
	const auto addh = unscaled.height() * added;
	return Rect(
		unscaled.x() - addw / 2,
		unscaled.y() - addh / 2,
		unscaled.width() + addw,
		unscaled.height() + addh);
}

} // namespace Media::View

#endif // Qt >= 6.7
