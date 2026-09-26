/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_coin_renderer.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/effects/premium_3d_mesh.h"
#include "base/debug_log.h"

#include <rhi/qrhi.h>
#include <QtGui/QImage>
#include <QtGui/QMatrix4x4>
#include <QtGui/QVector3D>

namespace Ui::Premium {
namespace {

constexpr auto kFloatsPerVertex = 8;
constexpr auto kModelScale = 1.f;
constexpr auto kFieldOfView = 53.13f;
constexpr auto kNearPlane = 1.f;
constexpr auto kFarPlane = 200.f;
constexpr auto kCameraDistance = 100.f;

struct alignas(16) SharedUniforms {
	float mvp[16];
	float world[16];
	float resolution[4];
	float misc[4]; // time, night, alpha, _
};
static_assert(sizeof(SharedUniforms) == 160);
static_assert(sizeof(SharedUniforms) % 16 == 0);

[[nodiscard]] QImage LoadBorderTexture() {
	return QImage(
		u":/gui/art/premium/coin_border.png"_q
	).convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

CoinRenderer::CoinRenderer() = default;

CoinRenderer::~CoinRenderer() {
	releaseResources();
}

void CoinRenderer::setState(State state) {
	_state = state;
}

void CoinRenderer::initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_initialized && _rhi == rhi) {
		return;
	}
	if (_rhi != rhi) {
		_creationFailed = false;
	}
	releaseResources();
	if (_creationFailed) {
		return;
	}
	_rhi = rhi;

	const auto model = LoadObject3dModel({
		u":/gui/art/premium/coin_outer.binobj"_q,
		u":/gui/art/premium/coin_inner.binobj"_q,
		u":/gui/art/premium/coin_logo.binobj"_q,
		u":/gui/art/premium/coin_stars.binobj"_q,
	}, kModelScale, true);
	if (model.isNull()) {
		LOG(("PremiumCoin: failed to load model"));
		_creationFailed = true;
		return;
	}

	for (auto i = 0; i != kShellCount; ++i) {
		const auto &shell = model.shells[i];
		_vertexCounts[i] = shell.vertexCount;
		_vertexBuffers[i] = rhi->newBuffer(
			QRhiBuffer::Immutable,
			QRhiBuffer::VertexBuffer,
			shell.vertices.size() * sizeof(float));
		if (!_vertexBuffers[i]->create()) {
			LOG(("PremiumCoin: vertex buffer creation failed"));
			_creationFailed = true;
			releaseResources();
			return;
		}
	}

	_sharedUniform = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(SharedUniforms));
	if (!_sharedUniform->create()) {
		_creationFailed = true;
		releaseResources();
		return;
	}

	const auto align = std::max<quint32>(rhi->ubufAlignment(), 1);
	const auto slotBytes = quint32(4 * sizeof(float));
	_drawStride = ((slotBytes + align - 1) / align) * align;
	_drawUniform = rhi->newBuffer(
		QRhiBuffer::Immutable,
		QRhiBuffer::UniformBuffer,
		kShellCount * _drawStride);
	if (!_drawUniform->create()) {
		_creationFailed = true;
		releaseResources();
		return;
	}

	const auto border = LoadBorderTexture();
	_borderTexture = rhi->newTexture(QRhiTexture::RGBA8, border.size());
	_borderTexture->create();
	_borderSampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	_borderSampler->create();

	const auto flecks = LoadObject3dFlecksTexture();
	_flecksTexture = rhi->newTexture(QRhiTexture::RGBA8, flecks.size());
	_flecksTexture->create();
	_flecksSampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::Repeat,
		QRhiSampler::Repeat);
	_flecksSampler->create();

	if (!createPipeline(rt)) {
		LOG(("PremiumCoin: pipeline creation failed, disabling effect"));
		_creationFailed = true;
		releaseResources();
		return;
	}

	const auto slotFloats = _drawStride / sizeof(float);
	auto drawData = std::vector<float>(kShellCount * slotFloats, 0.f);
	for (auto i = 0; i != kShellCount; ++i) {
		drawData[i * slotFloats + 0] = float(i);
	}

	auto *rub = rhi->nextResourceUpdateBatch();
	for (auto i = 0; i != kShellCount; ++i) {
		rub->uploadStaticBuffer(
			_vertexBuffers[i],
			model.shells[i].vertices.data());
	}
	rub->uploadStaticBuffer(_drawUniform, drawData.data());
	rub->uploadTexture(_borderTexture, border);
	rub->uploadTexture(_flecksTexture, flecks);
	cb->resourceUpdate(rub);

	_initialized = true;

	LOG(("PremiumCoin: initialized, backend=%1 device=%2")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

bool CoinRenderer::createPipeline(QRhiRenderTarget *rt) {
	const auto vertShader = LoadObject3dShader(u"premium_coin.vert"_q);
	const auto fragShader = LoadObject3dShader(u"premium_coin.frag"_q);
	if (!vertShader.isValid() || !fragShader.isValid()) {
		return false;
	}

	for (auto i = 0; i != kShellCount; ++i) {
		_srb[i] = _rhi->newShaderResourceBindings();
		_srb[i]->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_sharedUniform),
			QRhiShaderResourceBinding::uniformBuffer(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_drawUniform,
				i * _drawStride,
				quint32(4 * sizeof(float))),
			QRhiShaderResourceBinding::sampledTexture(
				2,
				QRhiShaderResourceBinding::FragmentStage,
				_borderTexture,
				_borderSampler),
			QRhiShaderResourceBinding::sampledTexture(
				3,
				QRhiShaderResourceBinding::FragmentStage,
				_flecksTexture,
				_flecksSampler),
		});
		if (!_srb[i]->create()) {
			return false;
		}
	}

	auto inputLayout = QRhiVertexInputLayout();
	inputLayout.setBindings({
		{ quint32(kFloatsPerVertex * sizeof(float)) },
	});
	inputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float3, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
		{ 0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float) },
	});

	auto blend = QRhiGraphicsPipeline::TargetBlend();
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

	_pipeline = _rhi->newGraphicsPipeline();
	_pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, vertShader },
		{ QRhiShaderStage::Fragment, fragShader },
	});
	_pipeline->setVertexInputLayout(inputLayout);
	_pipeline->setTargetBlends({ blend });
	_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
	_pipeline->setCullMode(QRhiGraphicsPipeline::None);
	_pipeline->setDepthTest(true);
	_pipeline->setDepthWrite(true);
	_pipeline->setSampleCount(rt->sampleCount());
	_pipeline->setShaderResourceBindings(_srb[0]);
	_pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
	return _pipeline->create();
}

void CoinRenderer::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (rhi->isDeviceLost()) {
		releaseResources();
		return;
	}
	if (!_initialized) {
		cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 });
		cb->endPass();
		return;
	}
	_rhi = rhi;

	const auto pixelSize = rt->pixelSize();
	if (pixelSize.isEmpty()) {
		return;
	}
	const auto aspect = float(pixelSize.width()) / float(pixelSize.height());

	auto projection = QMatrix4x4();
	projection.perspective(kFieldOfView, aspect, kNearPlane, kFarPlane);

	auto view = QMatrix4x4();
	view.lookAt(
		QVector3D(0.f, 0.f, kCameraDistance),
		QVector3D(0.f, 0.f, 0.f),
		QVector3D(0.f, 1.f, 0.f));

	auto world = QMatrix4x4();
	world.rotate(-_state.pitch, 1.f, 0.f, 0.f);
	world.rotate(-_state.yaw, 0.f, 1.f, 0.f);

	const auto mvp = rhi->clipSpaceCorrMatrix() * projection * view * world;

	auto shared = SharedUniforms();
	memcpy(shared.mvp, mvp.constData(), sizeof(shared.mvp));
	memcpy(shared.world, world.constData(), sizeof(shared.world));
	shared.resolution[0] = float(pixelSize.width());
	shared.resolution[1] = float(pixelSize.height());
	shared.misc[0] = _state.time;
	shared.misc[1] = _state.night ? 1.f : 0.f;
	shared.misc[2] = _state.alpha;

	auto *rub = rhi->nextResourceUpdateBatch();
	rub->updateDynamicBuffer(_sharedUniform, 0, sizeof(shared), &shared);

	cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, rub);
	cb->setGraphicsPipeline(_pipeline);
	cb->setViewport({
		0.f,
		0.f,
		float(pixelSize.width()),
		float(pixelSize.height()) });
	for (auto i = 0; i != kShellCount; ++i) {
		cb->setShaderResources(_srb[i]);
		const QRhiCommandBuffer::VertexInput vbuf[] = {
			{ _vertexBuffers[i], 0 },
		};
		cb->setVertexInput(0, 1, vbuf);
		cb->draw(_vertexCounts[i]);
	}
	cb->endPass();
}

void CoinRenderer::releaseResources() {
	if (!_rhi) {
		return;
	}
	delete _pipeline;
	_pipeline = nullptr;
	for (auto i = 0; i != kShellCount; ++i) {
		delete _srb[i];
		_srb[i] = nullptr;
	}
	delete _flecksSampler;
	_flecksSampler = nullptr;
	delete _flecksTexture;
	_flecksTexture = nullptr;
	delete _borderSampler;
	_borderSampler = nullptr;
	delete _borderTexture;
	_borderTexture = nullptr;
	delete _drawUniform;
	_drawUniform = nullptr;
	delete _sharedUniform;
	_sharedUniform = nullptr;
	for (auto i = 0; i != kShellCount; ++i) {
		delete _vertexBuffers[i];
		_vertexBuffers[i] = nullptr;
		_vertexCounts[i] = 0;
	}

	_initialized = false;
	_rhi = nullptr;
}

} // namespace Ui::Premium

#endif // Qt >= 6.7
