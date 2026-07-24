/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_pip_rhi.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/rhi/rhi_shader.h"
#include "ui/widgets/shadow.h"
#include "media/streaming/media_streaming_common.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"

#include <rhi/qrhi.h>

namespace Media::View {
namespace {

using namespace Ui::GL;

struct PipUniforms {
	float viewport[2];
	float _pad0[2];
	float roundRect[4];
	float roundRadius;
	float _pad1[3];
	float fadeColor[4];
	float h_size[2];
	float _pad2[2];
	float h_extend[4];
	float h_components[4];
};
static_assert(sizeof(PipUniforms) % 16 == 0);

struct ImageUniforms {
	float viewport[2];
	float g_opacity;
	float o_opacity;
};
static_assert(sizeof(ImageUniforms) % 16 == 0);

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Ui::Rhi::ShaderFromFile(
		u":/shaders/"_q + name + u".qsb"_q);
}

[[nodiscard]] QRhiGraphicsPipeline *CreatePipeline(
		QRhi *rhi,
		QRhiRenderPassDescriptor *rpDesc,
		QRhiShaderResourceBindings *srb,
		const QShader &vertexShader,
		const QShader &fragmentShader,
		bool blending,
		int vertexStride,
		int attributeCount = 2,
		QRhiGraphicsPipeline::Topology topology
			= QRhiGraphicsPipeline::TriangleStrip) {
	auto pipeline = rhi->newGraphicsPipeline();

	pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, vertexShader },
		{ QRhiShaderStage::Fragment, fragmentShader },
	});

	QRhiVertexInputLayout inputLayout;
	inputLayout.setBindings({
		{ quint32(vertexStride) },
	});
	if (attributeCount == 2) {
		inputLayout.setAttributes({
			{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
			{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
		});
	} else if (attributeCount == 3) {
		inputLayout.setAttributes({
			{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
			{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
			{ 0, 2, QRhiVertexInputAttribute::Float2, 4 * sizeof(float) },
		});
	} else {
		Unexpected("Attribute count in PiP::RendererRhi::CreatePipeline.");
	}
	pipeline->setVertexInputLayout(inputLayout);

	if (blending) {
		QRhiGraphicsPipeline::TargetBlend blend;
		blend.enable = true;
		blend.srcColor = QRhiGraphicsPipeline::One;
		blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		blend.srcAlpha = QRhiGraphicsPipeline::One;
		blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
		pipeline->setTargetBlends({ blend });
	}

	pipeline->setTopology(topology);
	pipeline->setShaderResourceBindings(srb);
	pipeline->setRenderPassDescriptor(rpDesc);
	pipeline->create();

	return pipeline;
}

} // namespace

Pip::RendererRhi::RendererRhi(not_null<Pip*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		_radialImage.invalidate();
		_playbackImage.invalidate();
		_volumeControllerImage.invalidate();
		invalidateControls();
	}, _lifetime);
}

Pip::RendererRhi::~RendererRhi() {
	releaseResources();
}

void Pip::RendererRhi::initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_initialized && _rhi == rhi && _rt == rt) {
		return;
	}
	releaseResources();

	_rhi = rhi;
	_rt = rt;
	_cb = cb;

	_vertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		kMaxDraws * kVertexSlotSize);
	_vertexBuffer->create();

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
	createShadowTexture();
	_initialized = true;
}

void Pip::RendererRhi::createPipelines() {
	const auto rpDesc = _rt->renderPassDescriptor();

	const auto argb32Vert = LoadShader(u"argb32.vert"_q);
	const auto passthroughVert = LoadShader(u"passthrough.vert"_q);
	const auto argb32Frag = LoadShader(u"argb32.frag"_q);
	const auto pipArgb32Frag = LoadShader(u"pip_argb32.frag"_q);
	const auto controlsFrag = LoadShader(u"pip_controls.frag"_q);

	_argb32Srb = _rhi->newShaderResourceBindings();
	_argb32Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
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
	_argb32Srb->create();

	_argb32Pipeline = CreatePipeline(
		_rhi,
		rpDesc,
		_argb32Srb,
		passthroughVert,
		pipArgb32Frag,
		false,
		4 * sizeof(float));

	_imageSrb = _rhi->newShaderResourceBindings();
	_imageSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	_imageSrb->create();

	_imagePipeline = CreatePipeline(
		_rhi,
		rpDesc,
		_imageSrb,
		argb32Vert,
		argb32Frag,
		false,
		4 * sizeof(float));

	_imageBlendPipeline = CreatePipeline(
		_rhi,
		rpDesc,
		_imageSrb,
		argb32Vert,
		argb32Frag,
		true,
		4 * sizeof(float));

	const auto pipControlsVert = LoadShader(u"pip_controls.vert"_q);
	_controlsSrb = _rhi->newShaderResourceBindings();
	_controlsSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	_controlsSrb->create();

	_controlsPipeline = CreatePipeline(
		_rhi,
		rpDesc,
		_controlsSrb,
		pipControlsVert,
		controlsFrag,
		true,
		6 * sizeof(float),
		3);

	const auto pipYuv420Frag = LoadShader(u"pip_yuv420.frag"_q);
	const auto pipNv12Frag = LoadShader(u"pip_nv12.frag"_q);

	_yuv420Srb = _rhi->newShaderResourceBindings();
	_yuv420Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
		QRhiShaderResourceBinding::sampledTexture(
			4, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
	});
	_yuv420Srb->create();

	_yuv420Pipeline = CreatePipeline(
		_rhi, rpDesc, _yuv420Srb,
		passthroughVert, pipYuv420Frag, false, 4 * sizeof(float));

	_nv12Srb = _rhi->newShaderResourceBindings();
	_nv12Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3, QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture, _sampler),
	});
	_nv12Srb->create();

	_nv12Pipeline = CreatePipeline(
		_rhi, rpDesc, _nv12Srb,
		passthroughVert, pipNv12Frag, false, 4 * sizeof(float));
}

void Pip::RendererRhi::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	_rhi = rhi;
	_rt = rt;
	_cb = cb;
	_nextSrbIndex = 0;
	_drawCommands.clear();
	_nextDrawSlot = 0;

	const auto size = rt->pixelSize();
	const auto factor = float(
		_owner->_panel.widget()->devicePixelRatioF());
	if (_factor != factor) {
		_factor = factor;
		_ifactor = int(std::ceil(_factor));
		invalidateControls();
	}
	_viewport = QSize(
		int(size.width() / _factor),
		int(size.height() / _factor));

	_rub = rhi->nextResourceUpdateBatch();

	_owner->paint(this);

	const auto bg = QColor(0, 0, 0, 0);
	cb->beginPass(rt, bg, { 1.0f, 0 }, _rub);
	_rub = nullptr;

	for (const auto &cmd : _drawCommands) {
		cb->setGraphicsPipeline(cmd.pipeline);
		cb->setShaderResources(cmd.srb);
		cb->setViewport({
			0, 0,
			float(rt->pixelSize().width()),
			float(rt->pixelSize().height()) });
		const QRhiCommandBuffer::VertexInput vbufBinding(
			cmd.vertexBuffer, cmd.vertexOffset);
		cb->setVertexInput(0, 1, &vbufBinding);
		cb->draw(4);
	}
	_drawCommands.clear();

	cb->endPass();
}

void Pip::RendererRhi::releaseResources() {
	_drawCommands.clear();
	for (auto *srb : _srbPool) {
		delete srb;
	}
	_srbPool.clear();
	_nextSrbIndex = 0;

	_shadowImage.destroy();
	_radialImage.destroy();
	_controlsImage.destroy();
	_playbackImage.destroy();
	_volumeControllerImage.destroy();

	delete _argb32Pipeline;
	_argb32Pipeline = nullptr;
	delete _yuv420Pipeline;
	_yuv420Pipeline = nullptr;
	delete _nv12Pipeline;
	_nv12Pipeline = nullptr;
	delete _imagePipeline;
	_imagePipeline = nullptr;
	delete _imageBlendPipeline;
	_imageBlendPipeline = nullptr;
	delete _controlsPipeline;
	_controlsPipeline = nullptr;

	delete _argb32Srb;
	_argb32Srb = nullptr;
	delete _yuv420Srb;
	_yuv420Srb = nullptr;
	delete _nv12Srb;
	_nv12Srb = nullptr;
	delete _imageSrb;
	_imageSrb = nullptr;
	delete _controlsSrb;
	_controlsSrb = nullptr;

	delete _placeholderTexture;
	_placeholderTexture = nullptr;
	delete _rgbaTexture;
	_rgbaTexture = nullptr;
	_rgbaSize = QSize();
	_cacheKey = 0;
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
	_trackFrameIndex = -1;
	_chromaNV12 = false;
	_usingExternalVideoTextures = false;
#ifdef Q_OS_MAC
	_metalTextureCache.flush();
#endif // Q_OS_MAC

	delete _vertexBuffer;
	_vertexBuffer = nullptr;
	delete _uniformBuffer;
	_uniformBuffer = nullptr;
	delete _sampler;
	_sampler = nullptr;

	_initialized = false;
}

int Pip::RendererRhi::allocateDrawSlot() {
	return (_nextDrawSlot < kMaxDraws) ? _nextDrawSlot++ : -1;
}

QRhiShaderResourceBindings *Pip::RendererRhi::allocateSrb() {
	if (_nextSrbIndex < int(_srbPool.size())) {
		return _srbPool[_nextSrbIndex++];
	}
	auto *srb = _rhi->newShaderResourceBindings();
	_srbPool.push_back(srb);
	_nextSrbIndex++;
	return srb;
}

void Pip::RendererRhi::createShadowTexture() {
	const auto &shadow = PipShadow();
	const auto size = 2 * PipShadow().topLeft.size()
		+ QSize(st::roundRadiusLarge, st::roundRadiusLarge);
	auto image = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		Ui::Shadow::paint(
			p,
			QRect(QPoint(), size).marginsRemoved(shadow.extend),
			size.width(),
			shadow);
	}
	_shadowImage.setImage(std::move(image));
}

void Pip::RendererRhi::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	}
	geometry.rotation = (geometry.rotation + geometry.videoRotation) % 360;
	if (data.format == Streaming::FrameFormat::ARGB32) {
		if (data.image.isNull()) {
			return;
		}
		paintTransformedStaticContent(data.image, geometry);
		return;
	}
	const auto nativeTexture =
		(data.format == Streaming::FrameFormat::NativeTexture);
	const auto nv12 = nativeTexture
		|| (data.format == Streaming::FrameFormat::NV12);
	const auto yuv = data.yuv;
	const auto nv12changed = !nativeTexture && (_chromaNV12 != nv12);
	const auto upload = (_trackFrameIndex != data.index);
	_trackFrameIndex = data.index;

	if (upload) {
		auto zeroCopied = false;
#ifdef Q_OS_MAC
		if (nativeTexture && data.nativeFrame
			&& data.nativeFrame->pixelBuffer) {
			const auto ok = _metalTextureCache.createTexturesFromPixelBuffer(
					_rhi,
					data.nativeFrame->pixelBuffer,
					&_yTexture,
					&_uvTexture,
					&_lumaSize,
					&_chromaSize);
			if (ok) {
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
				yuv->y.data, yuv->y.stride * yuv->size.height());
			yDesc.setDataStride(yuv->y.stride);
			_rub->uploadTexture(_yTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, yDesc)));

			if (nv12) {
				if (!_uvTexture || nv12changed
					|| _chromaSize != yuv->chromaSize) {
					delete _uvTexture;
					_uvTexture = _rhi->newTexture(
						QRhiTexture::RG8, yuv->chromaSize);
					_uvTexture->create();
					_chromaSize = yuv->chromaSize;
				}
				auto uvDesc = QRhiTextureSubresourceUploadDescription(
					yuv->u.data, yuv->u.stride * yuv->chromaSize.height());
				uvDesc.setDataStride(yuv->u.stride);
				_rub->uploadTexture(_uvTexture,
					QRhiTextureUploadDescription(
						QRhiTextureUploadEntry(0, 0, uvDesc)));
			} else {
				if (!_uTexture || nv12changed
					|| _chromaSize != yuv->chromaSize) {
					delete _uTexture;
					_uTexture = _rhi->newTexture(
						QRhiTexture::R8, yuv->chromaSize);
					_uTexture->create();
					delete _vTexture;
					_vTexture = _rhi->newTexture(
						QRhiTexture::R8, yuv->chromaSize);
					_vTexture->create();
					_chromaSize = yuv->chromaSize;
				}
				auto uDesc = QRhiTextureSubresourceUploadDescription(
					yuv->u.data, yuv->u.stride * yuv->chromaSize.height());
				uDesc.setDataStride(yuv->u.stride);
				_rub->uploadTexture(_uTexture,
					QRhiTextureUploadDescription(
						QRhiTextureUploadEntry(0, 0, uDesc)));
				auto vDesc = QRhiTextureSubresourceUploadDescription(
					yuv->v.data, yuv->v.stride * yuv->chromaSize.height());
				vDesc.setDataStride(yuv->v.stride);
				_rub->uploadTexture(_vTexture,
					QRhiTextureUploadDescription(
						QRhiTextureUploadEntry(0, 0, vDesc)));
			}
		} // !zeroCopied
		_chromaNV12 = nv12;
		_usingExternalVideoTextures = zeroCopied;
	}

	_shadowImage.upload(_rhi, _rub);

	if (nv12) {
		const auto slot = allocateDrawSlot();
		if (slot < 0) {
			return;
		}
		auto *srb = allocateSrb();
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				slot * 256,
				sizeof(PipUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1, QRhiShaderResourceBinding::FragmentStage,
				_yTexture, _sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2, QRhiShaderResourceBinding::FragmentStage,
				_uvTexture, _sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3, QRhiShaderResourceBinding::FragmentStage,
				_shadowImage.texture()
					? _shadowImage.texture()
					: _placeholderTexture,
				_sampler),
		});
		srb->create();
		paintTransformedContent(_nv12Pipeline, srb, geometry, slot);
	} else {
		const auto slot = allocateDrawSlot();
		if (slot < 0) {
			return;
		}
		auto *srb = allocateSrb();
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				slot * 256,
				sizeof(PipUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1, QRhiShaderResourceBinding::FragmentStage,
				_yTexture, _sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2, QRhiShaderResourceBinding::FragmentStage,
				_uTexture, _sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3, QRhiShaderResourceBinding::FragmentStage,
				_vTexture, _sampler),
			QRhiShaderResourceBinding::sampledTexture(
				4, QRhiShaderResourceBinding::FragmentStage,
				_shadowImage.texture()
					? _shadowImage.texture()
					: _placeholderTexture,
				_sampler),
		});
		srb->create();
		paintTransformedContent(_yuv420Pipeline, srb, geometry, slot);
	}
}

void Pip::RendererRhi::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) {
	if (image.isNull() || !_argb32Pipeline) {
		return;
	}

	const auto cacheKey = image.cacheKey();
	if (_cacheKey != cacheKey) {
		_cacheKey = cacheKey;
		if (!_rgbaTexture || _rgbaSize != image.size()) {
			delete _rgbaTexture;
			_rgbaTexture = _rhi->newTexture(
				QRhiTexture::BGRA8,
				image.size());
			_rgbaTexture->create();
			_rgbaSize = image.size();
		}
		auto desc = QRhiTextureSubresourceUploadDescription(image);
		_rub->uploadTexture(
			_rgbaTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, desc)));
	}

	const auto slot = allocateDrawSlot();
	if (slot < 0) {
		return;
	}
	auto *srb = allocateSrb();
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			slot * 256,
			sizeof(PipUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_rgbaTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_shadowImage.texture()
				? _shadowImage.texture()
				: _placeholderTexture,
			_sampler),
	});
	srb->create();

	_shadowImage.upload(_rhi, _rub);

	paintTransformedContent(_argb32Pipeline, srb, geometry, slot);
}

void Pip::RendererRhi::paintTransformedContent(
		QRhiGraphicsPipeline *pipeline,
		QRhiShaderResourceBindings *srb,
		ContentGeometry geometry,
		int slot) {
	std::array<std::array<float, 2>, 4> rect = { {
		{ { -1.f, 1.f } },
		{ { 1.f, 1.f } },
		{ { 1.f, -1.f } },
		{ { -1.f, -1.f } },
	} };
	if (const auto shift = (geometry.rotation / 90); shift != 0) {
		std::rotate(begin(rect), begin(rect) + shift, end(rect));
	}
	const auto xscale = 1.f / geometry.inner.width();
	const auto yscale = 1.f / geometry.inner.height();
	const float coords[] = {
		rect[0][0], rect[0][1],
		-geometry.inner.x() * xscale,
		-geometry.inner.y() * yscale,

		rect[1][0], rect[1][1],
		(geometry.outer.width() - geometry.inner.x()) * xscale,
		-geometry.inner.y() * yscale,

		rect[3][0], rect[3][1],
		-geometry.inner.x() * xscale,
		(geometry.outer.height() - geometry.inner.y()) * yscale,

		rect[2][0], rect[2][1],
		(geometry.outer.width() - geometry.inner.x()) * xscale,
		(geometry.outer.height() - geometry.inner.y()) * yscale,
	};

	const auto vOffset = slot * kVertexSlotSize;
	const auto uOffset = slot * 256;
	_rub->updateDynamicBuffer(
		_vertexBuffer,
		vOffset,
		sizeof(coords),
		coords);

	const auto globalFactor = _factor;
	const auto fadeAlpha = float(
		st::radialBg->c.alphaF() * geometry.fade);
	const auto roundRect = transformRect(RoundingRect(geometry));

	PipUniforms uniforms{};
	uniforms.viewport[0] = _viewport.width() * _factor;
	uniforms.viewport[1] = _viewport.height() * _factor;
	uniforms.roundRect[0] = roundRect.left();
	uniforms.roundRect[1] = roundRect.top();
	uniforms.roundRect[2] = roundRect.width();
	uniforms.roundRect[3] = roundRect.height();
	uniforms.roundRadius = st::roundRadiusLarge * _factor;
	uniforms.fadeColor[0] = float(st::radialBg->c.redF() * fadeAlpha);
	uniforms.fadeColor[1] = float(st::radialBg->c.greenF() * fadeAlpha);
	uniforms.fadeColor[2] = float(st::radialBg->c.blueF() * fadeAlpha);
	uniforms.fadeColor[3] = fadeAlpha;
	const auto &shadowImg = _shadowImage.image();
	uniforms.h_size[0] = shadowImg.width();
	uniforms.h_size[1] = shadowImg.height();
	uniforms.h_extend[0] = PipShadow().extend.left() * globalFactor;
	uniforms.h_extend[1] = PipShadow().extend.top() * globalFactor;
	uniforms.h_extend[2] = PipShadow().extend.right() * globalFactor;
	uniforms.h_extend[3] = PipShadow().extend.bottom() * globalFactor;
	uniforms.h_components[0] = PipShadow().topLeft.width() * globalFactor;
	uniforms.h_components[1] = PipShadow().topLeft.height() * globalFactor;
	uniforms.h_components[2] = PipShadow().left.width() * globalFactor;
	uniforms.h_components[3] = PipShadow().top.height() * globalFactor;

	_rub->updateDynamicBuffer(
		_uniformBuffer,
		uOffset,
		sizeof(PipUniforms),
		&uniforms);

	_drawCommands.push_back({
		.pipeline = pipeline,
		.srb = srb,
		.vertexBuffer = _vertexBuffer,
		.vertexOffset = quint32(vOffset),
	});
}

void Pip::RendererRhi::paintRadialLoading(
		QRect inner,
		float64 controlsShown) {
	paintUsingRaster(_radialImage, inner, [&](QPainter &&p) {
		const auto newInner = QRect(QPoint(), inner.size());
		const auto fg = st::radialFg->c;
		const auto fade = st::radialBg->c;
		const auto fadeAlpha = controlsShown * fade.alphaF();
		const auto fgAlpha = 1. - fadeAlpha;
		const auto color = (fadeAlpha == 0.) ? fg : QColor(
			int(base::SafeRound(
				fg.red() * fgAlpha + fade.red() * fadeAlpha)),
			int(base::SafeRound(
				fg.green() * fgAlpha + fade.green() * fadeAlpha)),
			int(base::SafeRound(
				fg.blue() * fgAlpha + fade.blue() * fadeAlpha)),
			fg.alpha());

		_owner->paintRadialLoadingContent(p, newInner, color);
	}, true);
}

void Pip::RendererRhi::paintPlayback(QRect outer, float64 shown) {
	paintUsingRaster(_playbackImage, outer, [&](QPainter &&p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintPlaybackContent(p, newOuter, shown);
	}, true);
}

void Pip::RendererRhi::paintVolumeController(QRect outer, float64 shown) {
	paintUsingRaster(
		_volumeControllerImage,
		outer,
		[&](QPainter &&p) {
			const auto newOuter = QRect(QPoint(), outer.size());
			_owner->paintVolumeControllerContent(p, newOuter, shown);
		},
		true);
}

void Pip::RendererRhi::paintButtonsStart() {
	validateControls();
}

void Pip::RendererRhi::paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) {
	const auto tryIndex = [&](int stateIndex) -> std::optional<Control> {
		const auto result = ControlMeta(button.state, stateIndex);
		return (result.icon == &icon && result.iconOver == &iconOver)
			? std::make_optional(result)
			: std::nullopt;
	};
	const auto meta = tryIndex(0)
		? *tryIndex(0)
		: tryIndex(1)
		? *tryIndex(1)
		: *tryIndex(2);
	Assert(meta.icon == &icon && meta.iconOver == &iconOver);

	_controlsImage.upload(_rhi, _rub);
	if (!_controlsImage.texture()) {
		return;
	}

	const auto iconRect = _controlsImage.texturedRect(
		button.icon,
		_controlsTextures[meta.index * 2 + 0]);
	const auto overRect = _controlsImage.texturedRect(
		button.icon,
		_controlsTextures[meta.index * 2 + 1]);
	const auto geo = transformRect(iconRect.geometry);

	const float coords[] = {
		geo.left(), geo.top(),
		iconRect.texture.left(), iconRect.texture.bottom(),
		overRect.texture.left(), overRect.texture.bottom(),

		geo.right(), geo.top(),
		iconRect.texture.right(), iconRect.texture.bottom(),
		overRect.texture.right(), overRect.texture.bottom(),

		geo.left(), geo.bottom(),
		iconRect.texture.left(), iconRect.texture.top(),
		overRect.texture.left(), overRect.texture.top(),

		geo.right(), geo.bottom(),
		iconRect.texture.right(), iconRect.texture.top(),
		overRect.texture.right(), overRect.texture.top(),
	};
	const auto slot = allocateDrawSlot();
	if (slot < 0) {
		return;
	}
	const auto vOffset = slot * kVertexSlotSize;
	const auto uOffset = slot * 256;
	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, sizeof(coords), coords);

	ImageUniforms uniforms{};
	uniforms.viewport[0] = _viewport.width() * _factor;
	uniforms.viewport[1] = _viewport.height() * _factor;
	uniforms.g_opacity = float(shown);
	uniforms.o_opacity = float(over);
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(ImageUniforms), &uniforms);

	auto *srb = allocateSrb();
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
			_controlsImage.texture(),
			_sampler),
	});
	srb->create();

	_drawCommands.push_back({
		.pipeline = _controlsPipeline,
		.srb = srb,
		.vertexBuffer = _vertexBuffer,
		.vertexOffset = quint32(vOffset),
	});
}

auto Pip::RendererRhi::ControlMeta(OverState control, int index)
-> Control {
	Expects(index >= 0);

	switch (control) {
	case OverState::Close: Assert(index < 1); return {
		0,
		&st::pipCloseIcon,
		&st::pipCloseIconOver,
	};
	case OverState::Enlarge: Assert(index < 1); return {
		1,
		&st::pipEnlargeIcon,
		&st::pipEnlargeIconOver,
	};
	case OverState::VolumeToggle: Assert(index < 3); return {
		(2 + index),
		(index == 0
			? &st::pipVolumeIcon0
			: (index == 1)
			? &st::pipVolumeIcon1
			: &st::pipVolumeIcon2),
		(index == 0
			? &st::pipVolumeIcon0Over
			: (index == 1)
			? &st::pipVolumeIcon1Over
			: &st::pipVolumeIcon2Over),
	};
	case OverState::Other: Assert(index < 2); return {
		(5 + index),
		(index ? &st::pipPauseIcon : &st::pipPlayIcon),
		(index ? &st::pipPauseIconOver : &st::pipPlayIconOver),
	};
	}
	Unexpected("Control value in Pip::RendererRhi::ControlIndex.");
}

void Pip::RendererRhi::validateControls() {
	if (!_controlsImage.image().isNull()) {
		return;
	}
	const auto metas = {
		ControlMeta(OverState::Close),
		ControlMeta(OverState::Enlarge),
		ControlMeta(OverState::VolumeToggle),
		ControlMeta(OverState::VolumeToggle, 1),
		ControlMeta(OverState::VolumeToggle, 2),
		ControlMeta(OverState::Other),
		ControlMeta(OverState::Other, 1),
	};
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		Assert(meta.icon->size() == meta.iconOver->size());
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += 2 * meta.icon->height();
	}
	auto image = QImage(
		QSize(maxWidth, fullHeight) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		auto index = 0;
		auto height = 0;
		const auto paint = [&](not_null<const style::icon*> icon) {
			icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _ifactor,
				icon->size() * _ifactor);
			height += icon->height();
		};
		for (const auto &meta : metas) {
			paint(meta.icon);
			paint(meta.iconOver);
		}
	}
	_controlsImage.setImage(std::move(image));
}

void Pip::RendererRhi::invalidateControls() {
	_controlsImage.invalidate();
	ranges::fill(_controlsTextures, QRect());
}

void Pip::RendererRhi::paintUsingRaster(
		Ui::Rhi::Image &image,
		QRect rect,
		Fn<void(QPainter&&)> method,
		bool transparent) {
	auto raster = image.takeImage();
	const auto size = rect.size() * _ifactor;
	if (raster.width() < size.width()
		|| raster.height() < size.height()) {
		raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
		raster.setDevicePixelRatio(_factor);
		if (!transparent
			&& (raster.width() > size.width()
				|| raster.height() > size.height())) {
			raster.fill(Qt::transparent);
		}
	} else if (raster.devicePixelRatio() != _ifactor) {
		raster.setDevicePixelRatio(_ifactor);
	}

	if (transparent) {
		raster.fill(Qt::transparent);
	}
	method(QPainter(&raster));

	image.setImage(std::move(raster), size);
	image.upload(_rhi, _rub);

	const auto textured = image.texturedRect(
		rect,
		QRect(QPoint(), size));
	const auto geometry = transformRect(textured.geometry);
	const float coords[] = {
		geometry.left(), geometry.top(),
		textured.texture.left(), textured.texture.bottom(),

		geometry.right(), geometry.top(),
		textured.texture.right(), textured.texture.bottom(),

		geometry.left(), geometry.bottom(),
		textured.texture.left(), textured.texture.top(),

		geometry.right(), geometry.bottom(),
		textured.texture.right(), textured.texture.top(),
	};
	const auto slot = allocateDrawSlot();
	if (slot < 0) {
		return;
	}
	const auto vOffset = slot * kVertexSlotSize;
	const auto uOffset = slot * 256;
	_rub->updateDynamicBuffer(
		_vertexBuffer,
		vOffset,
		sizeof(coords),
		coords);

	ImageUniforms uniforms{};
	uniforms.viewport[0] = _viewport.width() * _factor;
	uniforms.viewport[1] = _viewport.height() * _factor;
	uniforms.g_opacity = 1.0f;

	_rub->updateDynamicBuffer(
		_uniformBuffer,
		uOffset,
		sizeof(ImageUniforms),
		&uniforms);

	if (image.texture()) {
		auto *srb = allocateSrb();
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
				image.texture(),
				_sampler),
		});
		srb->create();

		const auto pipeline = transparent
			? _imageBlendPipeline
			: _imagePipeline;
		_drawCommands.push_back({
			.pipeline = pipeline,
			.srb = srb,
			.vertexBuffer = _vertexBuffer,
			.vertexOffset = quint32(vOffset),
		});
	}
}

QRect Pip::RendererRhi::RoundingRect(ContentGeometry geometry) {
	const auto inner = geometry.inner;
	const auto attached = geometry.attached;
	const auto added = std::max({
		st::roundRadiusLarge,
		inner.x(),
		inner.y(),
		geometry.outer.width() - inner.x() - inner.width(),
		geometry.outer.height() - inner.y() - inner.height(),
		PipShadow().topLeft.width(),
		PipShadow().topLeft.height(),
		PipShadow().topRight.width(),
		PipShadow().topRight.height(),
		PipShadow().bottomRight.width(),
		PipShadow().bottomRight.height(),
		PipShadow().bottomLeft.width(),
		PipShadow().bottomLeft.height(),
	});
	return geometry.inner.marginsAdded({
		(attached & RectPart::Left) ? added : 0,
		(attached & RectPart::Top) ? added : 0,
		(attached & RectPart::Right) ? added : 0,
		(attached & RectPart::Bottom) ? added : 0,
	});
}

Rect Pip::RendererRhi::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect Pip::RendererRhi::transformRect(const QRectF &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect Pip::RendererRhi::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

} // namespace Media::View

#endif // Qt >= 6.7
