/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_diamond_renderer.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/effects/premium_3d_mesh.h"
#include "base/debug_log.h"

#include <rhi/qrhi.h>
#include <QtGui/QMatrix4x4>
#include <QtGui/QVector3D>

namespace Ui::Premium {
namespace {

constexpr auto kFloatsPerVertex = 6;
constexpr auto kModelScale = 8.f;
constexpr auto kFieldOfView = 12.f;
constexpr auto kNearPlane = 1.f;
constexpr auto kFarPlane = 200.f;
constexpr auto kCameraHeight = 40.f;
constexpr auto kCameraDistance = 100.f;

struct alignas(16) SharedUniforms {
	float mvp[16];
	float world[16];
	float resolution[4];
	float misc[4]; // time, night, alpha, _
};
static_assert(sizeof(SharedUniforms) == 160);
static_assert(sizeof(SharedUniforms) % 16 == 0);

} // namespace

DiamondRenderer::DiamondRenderer() = default;

DiamondRenderer::~DiamondRenderer() {
	releaseResources();
}

void DiamondRenderer::setState(State state) {
	_state = state;
}

void DiamondRenderer::initialize(
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
		u":/gui/art/premium/diamond_outer_2.binobj"_q,
		u":/gui/art/premium/diamond_outer.binobj"_q,
		u":/gui/art/premium/diamond.binobj"_q,
	}, kModelScale, false);
	if (model.isNull()) {
		LOG(("PremiumDiamond: failed to load model"));
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
			LOG(("PremiumDiamond: vertex buffer creation failed"));
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
		kDrawCount * _drawStride);
	if (!_drawUniform->create()) {
		_creationFailed = true;
		releaseResources();
		return;
	}

	if (!createPipelines(rt)) {
		LOG(("PremiumDiamond: pipeline creation failed, disabling effect"));
		_creationFailed = true;
		releaseResources();
		return;
	}

	const auto slotFloats = _drawStride / sizeof(float);
	auto drawData = std::vector<float>(kDrawCount * slotFloats, 0.f);
	for (auto i = 0; i != kDrawCount; ++i) {
		const auto base = i * slotFloats;
		drawData[base + 0] = float(kDrawSlots[i].shell);
		drawData[base + 1] = kDrawSlots[i].behind ? 1.f : 0.f;
	}

	auto *rub = rhi->nextResourceUpdateBatch();
	for (auto i = 0; i != kShellCount; ++i) {
		rub->uploadStaticBuffer(
			_vertexBuffers[i],
			model.shells[i].vertices.data());
	}
	rub->uploadStaticBuffer(_drawUniform, drawData.data());
	cb->resourceUpdate(rub);

	_initialized = true;

	LOG(("PremiumDiamond: initialized, backend=%1 device=%2")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

bool DiamondRenderer::createPipelines(QRhiRenderTarget *rt) {
	const auto vertShader = LoadObject3dShader(u"premium_diamond.vert"_q);
	const auto fragShader = LoadObject3dShader(u"premium_diamond.frag"_q);
	if (!vertShader.isValid() || !fragShader.isValid()) {
		return false;
	}

	for (auto i = 0; i != kDrawCount; ++i) {
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
	});

	auto blend = QRhiGraphicsPipeline::TargetBlend();
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

	const auto makePipeline = [&](bool depth) -> QRhiGraphicsPipeline* {
		auto pipeline = _rhi->newGraphicsPipeline();
		pipeline->setShaderStages({
			{ QRhiShaderStage::Vertex, vertShader },
			{ QRhiShaderStage::Fragment, fragShader },
		});
		pipeline->setVertexInputLayout(inputLayout);
		pipeline->setTargetBlends({ blend });
		pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
		pipeline->setCullMode(QRhiGraphicsPipeline::Back);
		pipeline->setDepthTest(depth);
		pipeline->setDepthWrite(depth);
		pipeline->setSampleCount(rt->sampleCount());
		pipeline->setShaderResourceBindings(_srb[0]);
		pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
		if (!pipeline->create()) {
			delete pipeline;
			return nullptr;
		}
		return pipeline;
	};

	_behindPipeline = makePipeline(false);
	_frontPipeline = makePipeline(true);
	return (_behindPipeline != nullptr) && (_frontPipeline != nullptr);
}

void DiamondRenderer::render(
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
		QVector3D(0.f, kCameraHeight, kCameraDistance),
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
	const auto viewport = QRhiViewport(
		0.f,
		0.f,
		float(pixelSize.width()),
		float(pixelSize.height()));
	for (auto i = 0; i != kDrawCount; ++i) {
		const auto &slot = kDrawSlots[i];
		cb->setGraphicsPipeline(slot.behind
			? _behindPipeline
			: _frontPipeline);
		cb->setViewport(viewport);
		cb->setShaderResources(_srb[i]);
		const QRhiCommandBuffer::VertexInput vbuf[] = {
			{ _vertexBuffers[slot.shell], 0 },
		};
		cb->setVertexInput(0, 1, vbuf);
		cb->draw(_vertexCounts[slot.shell]);
	}
	cb->endPass();
}

void DiamondRenderer::releaseResources() {
	if (!_rhi) {
		return;
	}
	delete _behindPipeline;
	_behindPipeline = nullptr;
	delete _frontPipeline;
	_frontPipeline = nullptr;
	for (auto i = 0; i != kDrawCount; ++i) {
		delete _srb[i];
		_srb[i] = nullptr;
	}
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
