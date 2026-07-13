/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/thanos_effect_renderer.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/rhi/rhi_shader.h"
#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "base/debug_log.h"

#include <rhi/qrhi.h>

namespace Ui {
namespace {

constexpr auto kQuadVertexCount = int(6);
constexpr auto kQuadVertexStride = int(2 * sizeof(float));
constexpr auto kComputeWorkgroupSize = int(64);
constexpr auto kMaxPhaseDuration = float64(6.);
constexpr auto kPhaseSpeed = float64(1.65);
constexpr auto kTimeStepMultiplier = float64(1.65);
constexpr auto kAccelerationStartPhase = float64(1.);
constexpr auto kAccelerationRampPhase = float64(2.5);
constexpr auto kAccelerationMaxMultiplier = float64(2.2);
constexpr auto kDisappearStartPhase = kMaxPhaseDuration * 0.15;
constexpr auto kDisappearDuration
	= kMaxPhaseDuration - kDisappearStartPhase;
constexpr auto kMaxParticleCount = uint32_t(120000);
static_assert(kMaxParticleCount < (1u << 30));

const float kQuadVertices[kQuadVertexCount * 2] = {
	0.f, 0.f,
	1.f, 0.f,
	0.f, 1.f,
	1.f, 0.f,
	0.f, 1.f,
	1.f, 1.f,
};

struct alignas(16) ComputeInitUniforms {
	uint32_t particleCountX;
	uint32_t particleCountY;
	uint32_t seed;
	uint32_t _pad;
};
static_assert(sizeof(ComputeInitUniforms) % 16 == 0);

struct alignas(16) ComputeUpdateUniforms {
	uint32_t particleCountX;
	uint32_t particleCountY;
	float phase;
	float timeStep;
};
static_assert(sizeof(ComputeUpdateUniforms) % 16 == 0);

struct alignas(16) RenderUniforms {
	float rect[4];
	float size[2];
	uint32_t particleResolution[2];
	float scale[4];
};
static_assert(sizeof(RenderUniforms) % 16 == 0);

[[nodiscard]] float64 AnimationSpeedMultiplier(float64 phase) {
	if (phase <= kAccelerationStartPhase) {
		return 1.;
	}
	const auto t = std::clamp(
		(phase - kAccelerationStartPhase) / kAccelerationRampPhase,
		0.,
		1.);
	const auto smooth = t * t * t * (t * (t * 6. - 15.) + 10.);
	return 1. + ((kAccelerationMaxMultiplier - 1.) * smooth);
}

[[nodiscard]] float64 DisappearProgress(float64 phase) {
	const auto t = std::clamp(
		(phase - kDisappearStartPhase) / kDisappearDuration,
		0.,
		1.);
	const auto oneMinus = 1. - t;
	return 1. - (oneMinus * oneMinus * oneMinus);
}

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Rhi::ShaderFromFile(u":/shaders/"_q + name + u".qsb"_q);
}

} // namespace

ThanosEffectRenderer::ThanosEffectRenderer(
		rpl::producer<float64> devicePixelRatio) {
	std::move(
		devicePixelRatio
	) | rpl::on_next([=, this](float64 value) {
		_factor = value;
	}, _lifetime);
}

ThanosEffectRenderer::~ThanosEffectRenderer() {
	releaseResources();
}

void ThanosEffectRenderer::initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_initialized && _rhi == rhi) {
		return;
	}
	// A different rhi instance (fresh setup or post device-lost recovery)
	// is a chance to retry pipeline creation; only an in-place repeat with
	// the same instance keeps the sticky failure.
	if (_rhi != rhi) {
		_creationFailed = false;
	}
	releaseResources();
	if (_creationFailed) {
		return;
	}
	_rhi = rhi;

	if (!rhi->isFeatureSupported(QRhi::Compute)) {
		LOG(("ThanosEffect: Compute shaders not supported, disabled"));
		return;
	}

	_quadVertexBuffer = rhi->newBuffer(
		QRhiBuffer::Immutable,
		QRhiBuffer::VertexBuffer,
		sizeof(kQuadVertices));
	_quadVertexBuffer->create();

	_computeInitUniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(ComputeInitUniforms));
	_computeInitUniformBuffer->create();

	_computeUpdateUniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(ComputeUpdateUniforms));
	_computeUpdateUniformBuffer->create();

	_renderUniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(RenderUniforms));
	_renderUniformBuffer->create();

	_placeholderTexture = rhi->newTexture(
		QRhiTexture::RGBA8,
		QSize(1, 1));
	_placeholderTexture->create();

	_placeholderSampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_placeholderSampler->create();

	_placeholderStateTexture = rhi->newTexture(
		QRhiTexture::RGBA32F,
		QSize(1, 1),
		1,
		QRhiTexture::UsedWithLoadStore);
	_placeholderStateTexture->create();

	_placeholderStateSampler = rhi->newSampler(
		QRhiSampler::Nearest,
		QRhiSampler::Nearest,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_placeholderStateSampler->create();

	if (!createPipelines(rt)) {
		LOG(("ThanosEffect: pipeline creation failed, disabling effect"));
		_creationFailed = true;
		releaseResources();
		return;
	}

	auto *rub = rhi->nextResourceUpdateBatch();
	rub->uploadStaticBuffer(_quadVertexBuffer, kQuadVertices);
	cb->resourceUpdate(rub);

	_initialized = true;
	_lastFrameTime = crl::now();

	LOG(("ThanosEffect: initialized, backend=%1 device=%2")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

bool ThanosEffectRenderer::createPipelines(QRhiRenderTarget *rt) {
	const auto initShader = LoadShader(u"thanos_init.comp"_q);
	const auto updateShader = LoadShader(u"thanos_update.comp"_q);
	const auto vertShader = LoadShader(u"thanos.vert"_q);
	const auto fragShader = LoadShader(u"thanos.frag"_q);

	_computeInitSrbLayout = _rhi->newShaderResourceBindings();
	_computeInitSrbLayout->setBindings({
		QRhiShaderResourceBinding::imageLoadStore(
			0,
			QRhiShaderResourceBinding::ComputeStage,
			_placeholderStateTexture,
			0),
		QRhiShaderResourceBinding::uniformBuffer(
			1,
			QRhiShaderResourceBinding::ComputeStage,
			_computeInitUniformBuffer),
		QRhiShaderResourceBinding::imageLoadStore(
			2,
			QRhiShaderResourceBinding::ComputeStage,
			_placeholderStateTexture,
			0),
	});
	_computeInitSrbLayout->create();

	_computeInitPipeline = _rhi->newComputePipeline();
	_computeInitPipeline->setShaderStage(
		{ QRhiShaderStage::Compute, initShader });
	_computeInitPipeline->setShaderResourceBindings(_computeInitSrbLayout);
	if (!_computeInitPipeline->create()) {
		return false;
	}

	_computeUpdateSrbLayout = _rhi->newShaderResourceBindings();
	_computeUpdateSrbLayout->setBindings({
		QRhiShaderResourceBinding::imageLoadStore(
			0,
			QRhiShaderResourceBinding::ComputeStage,
			_placeholderStateTexture,
			0),
		QRhiShaderResourceBinding::uniformBuffer(
			1,
			QRhiShaderResourceBinding::ComputeStage,
			_computeUpdateUniformBuffer),
		QRhiShaderResourceBinding::imageLoadStore(
			2,
			QRhiShaderResourceBinding::ComputeStage,
			_placeholderStateTexture,
			0),
	});
	_computeUpdateSrbLayout->create();

	_computeUpdatePipeline = _rhi->newComputePipeline();
	_computeUpdatePipeline->setShaderStage(
		{ QRhiShaderStage::Compute, updateShader });
	_computeUpdatePipeline->setShaderResourceBindings(
		_computeUpdateSrbLayout);
	if (!_computeUpdatePipeline->create()) {
		return false;
	}

	_renderSrbLayout = _rhi->newShaderResourceBindings();
	_renderSrbLayout->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage,
			_renderUniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_placeholderSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::VertexStage,
			_placeholderStateTexture,
			_placeholderStateSampler),
	});
	_renderSrbLayout->create();

	_renderPipeline = _rhi->newGraphicsPipeline();
	_renderPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, vertShader },
		{ QRhiShaderStage::Fragment, fragShader },
	});

	QRhiVertexInputLayout inputLayout;
	inputLayout.setBindings({
		{ quint32(kQuadVertexStride) },
	});
	inputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
	});
	_renderPipeline->setVertexInputLayout(inputLayout);

	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	_renderPipeline->setTargetBlends({ blend });

	_renderPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
	_renderPipeline->setShaderResourceBindings(_renderSrbLayout);
	_renderPipeline->setRenderPassDescriptor(
		rt->renderPassDescriptor());
	if (!_renderPipeline->create()) {
		return false;
	}
	return true;
}

void ThanosEffectRenderer::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (rhi->isDeviceLost()) {
		_pendingItems.clear();
		releaseResources();
		return;
	}
	if (!_initialized || !rhi->isFeatureSupported(QRhi::Compute)) {
		_pendingItems.clear();
		return;
	}
	_rhi = rhi;

	const auto now = crl::now();
	// Cap to ~15 FPS so a single slow frame cannot teleport particles.
	const auto dt = std::clamp(
		now - _lastFrameTime,
		crl::time(1),
		crl::time(66)) / 1000.;
	_lastFrameTime = now;

	addPendingItems(cb);

	if (_items.empty()) {
		return;
	}

	const auto pixelSize = rt->pixelSize();
	const auto viewW = float64(pixelSize.width()) / _factor;
	const auto viewH = float64(pixelSize.height()) / _factor;

	{
		auto *rub = rhi->nextResourceUpdateBatch();
		auto needsInit = false;

		for (auto &item : _items) {
			if (item.phase >= kMaxPhaseDuration) {
				continue;
			}
			const auto animationTimeStep
				= dt * AnimationSpeedMultiplier(item.phase);
			item.phase += animationTimeStep * kPhaseSpeed;
			if (!item.particlesInitialized) {
				needsInit = true;
				item.particlesInitialized = true;
				item.needsInitDispatch = true;

				ComputeInitUniforms uni;
				uni.particleCountX = item.particleCountX;
				uni.particleCountY = item.particleCountY;
				uni.seed = _seedCounter++;
				uni._pad = 0;
				rub->updateDynamicBuffer(
					item.computeInitUniformBuffer,
					0,
					sizeof(uni),
					&uni);
			}

			ComputeUpdateUniforms updateUni;
			updateUni.particleCountX = item.particleCountX;
			updateUni.particleCountY = item.particleCountY;
			updateUni.phase = float(item.phase);
			updateUni.timeStep = float(
				animationTimeStep * kTimeStepMultiplier);
			rub->updateDynamicBuffer(
				item.computeUpdateUniformBuffer,
				0,
				sizeof(updateUni),
				&updateUni);
		}

		if (needsInit) {
			cb->beginComputePass(rub);
			rub = nullptr;
			for (auto &item : _items) {
				if (!item.needsInitDispatch) {
					continue;
				}
				item.needsInitDispatch = false;
				cb->setComputePipeline(_computeInitPipeline);
				cb->setShaderResources(item.computeInitSrb);
				const auto count =
					item.particleCountX * item.particleCountY;
				const auto groups =
					(count + kComputeWorkgroupSize - 1)
					/ kComputeWorkgroupSize;
				cb->dispatch(int(groups), 1, 1);
			}
			cb->endComputePass();
		}

		cb->beginComputePass(rub);
		for (auto &item : _items) {
			if (item.phase >= kMaxPhaseDuration) {
				continue;
			}
			cb->setComputePipeline(_computeUpdatePipeline);
			cb->setShaderResources(item.computeUpdateSrb);
			const auto count = item.particleCountX * item.particleCountY;
			const auto groups = (count + kComputeWorkgroupSize - 1)
				/ kComputeWorkgroupSize;
			cb->dispatch(int(groups), 1, 1);
		}
		cb->endComputePass();
	}

	{
		auto *renderRub = rhi->nextResourceUpdateBatch();
		for (auto &item : _items) {
			if (item.phase >= kMaxPhaseDuration) {
				continue;
			}
			RenderUniforms uni;
			uni.rect[0] = float(item.rect.x() / viewW);
			uni.rect[1] = float(
				(viewH - item.rect.y() - item.rect.height()) / viewH);
			uni.rect[2] = float(item.rect.width() / viewW);
			uni.rect[3] = float(item.rect.height() / viewH);
			uni.size[0] = float(item.rect.width());
			uni.size[1] = float(item.rect.height());
			uni.particleResolution[0] = item.particleCountX;
			uni.particleResolution[1] = item.particleCountY;
			const auto inverseDisappear = float(
				1. - DisappearProgress(item.phase));
			uni.scale[0] = inverseDisappear;
			uni.scale[1] = 0.;
			uni.scale[2] = inverseDisappear;
			uni.scale[3] = 0.;

			renderRub->updateDynamicBuffer(
				item.renderUniformBuffer,
				0,
				sizeof(uni),
				&uni);
		}

		const auto bg = QColor(0, 0, 0, 0);
		cb->beginPass(rt, bg, { 1.0f, 0 }, renderRub);

		for (auto &item : _items) {
			if (item.phase >= kMaxPhaseDuration) {
				continue;
			}
			cb->setGraphicsPipeline(_renderPipeline);
			cb->setShaderResources(item.renderSrb);
			cb->setViewport({
				0, 0,
				float(pixelSize.width()),
				float(pixelSize.height()) });

			const QRhiCommandBuffer::VertexInput vbufs[] = {
				{ _quadVertexBuffer, 0 },
			};
			cb->setVertexInput(0, 1, vbufs);

			const auto instanceCount =
				item.particleCountX * item.particleCountY;
			cb->draw(kQuadVertexCount, instanceCount);
		}

		cb->endPass();
	}

	auto hadItems = !_items.empty();
	_items.erase(
		std::remove_if(_items.begin(), _items.end(), [&](auto &item) {
			if (item.phase >= kMaxPhaseDuration) {
				destroyAnimatingItem(item);
				return true;
			}
			return false;
		}),
		_items.end());

	if (hadItems && _items.empty()) {
		_allDone.fire({});
	}
}

void ThanosEffectRenderer::addItem(ThanosItem item) {
	_pendingItems.push_back(std::move(item));
}

bool ThanosEffectRenderer::hasActiveItems() const {
	return !_items.empty() || !_pendingItems.empty();
}

rpl::producer<> ThanosEffectRenderer::allDone() const {
	return _allDone.events();
}

void ThanosEffectRenderer::addPendingItems(QRhiCommandBuffer *cb) {
	if (_pendingItems.empty() || !_rhi) {
		return;
	}

	auto *rub = _rhi->nextResourceUpdateBatch();

	for (auto &pending : _pendingItems) {
		auto animating = createAnimatingItem(std::move(pending));
		if (animating.texture) {
			auto image = animating.uploadImage;
			if (!image.isNull()) {
				rub->uploadTexture(
					animating.texture,
					QRhiTextureUploadDescription(
						QRhiTextureUploadEntry(
							0, 0,
							QRhiTextureSubresourceUploadDescription(
								image))));
			}
			animating.uploadImage = QImage();
			_items.push_back(std::move(animating));
		}
	}
	_pendingItems.clear();

	cb->resourceUpdate(rub);
}

ThanosEffectRenderer::AnimatingItem ThanosEffectRenderer::createAnimatingItem(
		ThanosItem &&item) {
	AnimatingItem result;
	result.rect = item.rect;

	const auto w = int(item.rect.width());
	const auto h = int(item.rect.height());
	if (w <= 0 || h <= 0 || item.snapshot.isNull()) {
		return result;
	}

	const auto totalPixels = uint32_t(w) * uint32_t(h);
	if (totalPixels <= kMaxParticleCount) {
		result.particleCountX = uint32_t(w);
		result.particleCountY = uint32_t(h);
	} else {
		const auto aspectRatio = float64(w) / float64(h);
		const auto maxParticles = float64(kMaxParticleCount);
		result.particleCountY = std::max(
			uint32_t(1),
			uint32_t(std::sqrt(maxParticles / aspectRatio)));
		result.particleCountX = std::max(
			uint32_t(1),
			uint32_t(maxParticles / float64(result.particleCountY)));
	}

	auto *tex = _rhi->newTexture(
		QRhiTexture::RGBA8,
		QSize(item.snapshot.width(), item.snapshot.height()));
	if (!tex->create()) {
		delete tex;
		return result;
	}
	result.texture = tex;

	result.uploadImage = item.snapshot.convertToFormat(
		QImage::Format_RGBA8888_Premultiplied);

	auto *sampler = _rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	if (!sampler->create()) {
		delete sampler;
		destroyAnimatingItem(result);
		return result;
	}
	result.sampler = sampler;

	auto *stateTex = _rhi->newTexture(
		QRhiTexture::RGBA32F,
		QSize(int(result.particleCountX), int(result.particleCountY)),
		1,
		QRhiTexture::UsedWithLoadStore);
	if (!stateTex->create()) {
		delete stateTex;
		destroyAnimatingItem(result);
		return result;
	}
	result.particleStateTexture = stateTex;

	auto *velocityTex = _rhi->newTexture(
		QRhiTexture::RGBA32F,
		QSize(int(result.particleCountX), int(result.particleCountY)),
		1,
		QRhiTexture::UsedWithLoadStore);
	if (!velocityTex->create()) {
		delete velocityTex;
		destroyAnimatingItem(result);
		return result;
	}
	result.particleVelocityTexture = velocityTex;

	auto *stateSampler = _rhi->newSampler(
		QRhiSampler::Nearest,
		QRhiSampler::Nearest,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	if (!stateSampler->create()) {
		delete stateSampler;
		destroyAnimatingItem(result);
		return result;
	}
	result.particleStateSampler = stateSampler;

	auto *initUbo = _rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(ComputeInitUniforms));
	if (!initUbo->create()) {
		delete initUbo;
		destroyAnimatingItem(result);
		return result;
	}
	result.computeInitUniformBuffer = initUbo;

	auto *updateUbo = _rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(ComputeUpdateUniforms));
	if (!updateUbo->create()) {
		delete updateUbo;
		destroyAnimatingItem(result);
		return result;
	}
	result.computeUpdateUniformBuffer = updateUbo;

	result.computeInitSrb = _rhi->newShaderResourceBindings();
	result.computeInitSrb->setBindings({
		QRhiShaderResourceBinding::imageLoadStore(
			0,
			QRhiShaderResourceBinding::ComputeStage,
			stateTex,
			0),
		QRhiShaderResourceBinding::uniformBuffer(
			1,
			QRhiShaderResourceBinding::ComputeStage,
			initUbo),
		QRhiShaderResourceBinding::imageLoadStore(
			2,
			QRhiShaderResourceBinding::ComputeStage,
			velocityTex,
			0),
	});
	if (!result.computeInitSrb->create()) {
		destroyAnimatingItem(result);
		return result;
	}

	result.computeUpdateSrb = _rhi->newShaderResourceBindings();
	result.computeUpdateSrb->setBindings({
		QRhiShaderResourceBinding::imageLoadStore(
			0,
			QRhiShaderResourceBinding::ComputeStage,
			stateTex,
			0),
		QRhiShaderResourceBinding::uniformBuffer(
			1,
			QRhiShaderResourceBinding::ComputeStage,
			updateUbo),
		QRhiShaderResourceBinding::imageLoadStore(
			2,
			QRhiShaderResourceBinding::ComputeStage,
			velocityTex,
			0),
	});
	if (!result.computeUpdateSrb->create()) {
		destroyAnimatingItem(result);
		return result;
	}

	auto *renderUbo = _rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(RenderUniforms));
	if (!renderUbo->create()) {
		delete renderUbo;
		destroyAnimatingItem(result);
		return result;
	}
	result.renderUniformBuffer = renderUbo;

	result.renderSrb = _rhi->newShaderResourceBindings();
	result.renderSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage,
			renderUbo),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			tex,
			sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::VertexStage,
			stateTex,
			stateSampler),
	});
	if (!result.renderSrb->create()) {
		destroyAnimatingItem(result);
		return result;
	}

	return result;
}

void ThanosEffectRenderer::destroyAnimatingItem(AnimatingItem &item) {
	const auto deferDelete = [](auto *&resource) {
		if (resource) {
			resource->deleteLater();
			resource = nullptr;
		}
	};
	deferDelete(item.renderSrb);
	deferDelete(item.computeUpdateSrb);
	deferDelete(item.computeInitSrb);
	deferDelete(item.renderUniformBuffer);
	deferDelete(item.computeUpdateUniformBuffer);
	deferDelete(item.computeInitUniformBuffer);
	deferDelete(item.particleStateSampler);
	deferDelete(item.particleVelocityTexture);
	deferDelete(item.particleStateTexture);
	deferDelete(item.sampler);
	deferDelete(item.texture);
	item = {};
}

void ThanosEffectRenderer::releaseResources() {
	if (!_rhi) {
		return;
	}
	for (auto &item : _items) {
		destroyAnimatingItem(item);
	}
	_items.clear();
	_pendingItems.clear();

	delete _renderPipeline;
	_renderPipeline = nullptr;
	delete _renderSrbLayout;
	_renderSrbLayout = nullptr;
	delete _computeUpdatePipeline;
	_computeUpdatePipeline = nullptr;
	delete _computeUpdateSrbLayout;
	_computeUpdateSrbLayout = nullptr;
	delete _computeInitPipeline;
	_computeInitPipeline = nullptr;
	delete _computeInitSrbLayout;
	_computeInitSrbLayout = nullptr;

	delete _placeholderStateSampler;
	_placeholderStateSampler = nullptr;
	delete _placeholderStateTexture;
	_placeholderStateTexture = nullptr;
	delete _placeholderSampler;
	_placeholderSampler = nullptr;
	delete _placeholderTexture;
	_placeholderTexture = nullptr;

	delete _renderUniformBuffer;
	_renderUniformBuffer = nullptr;
	delete _computeUpdateUniformBuffer;
	_computeUpdateUniformBuffer = nullptr;
	delete _computeInitUniformBuffer;
	_computeInitUniformBuffer = nullptr;
	delete _quadVertexBuffer;
	_quadVertexBuffer = nullptr;

	_initialized = false;
	_seedCounter = 0;
	_rhi = nullptr;
}

} // namespace Ui

#endif // Qt >= 6.7
