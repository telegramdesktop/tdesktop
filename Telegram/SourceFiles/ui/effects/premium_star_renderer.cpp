/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_star_renderer.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

#include "ui/effects/premium_star_model.h"
#include "ui/effects/premium_3d_mesh.h"
#include "base/debug_log.h"

#include <rhi/qrhi.h>
#include <QtGui/QImage>
#include <QtGui/QMatrix4x4>
#include <QtGui/QPainter>
#include <QtGui/QVector3D>
#include <QtSvg/QSvgRenderer>

namespace Ui::Premium {
namespace {

constexpr auto kFloatsPerVertex = 8;
constexpr auto kBorderTextureSize = 240;
constexpr auto kSpec1 = 2.f;
constexpr auto kSpec2 = 0.13f;
constexpr auto kDiffuse = 0.8f;
constexpr auto kNormalSpec = 0.2f;
constexpr auto kFieldOfView = 53.13f;
constexpr auto kNearPlane = 1.f;
constexpr auto kFarPlane = 200.f;
constexpr auto kCameraDistance = 100.f;

struct alignas(16) StarUniforms {
	float mvp[16];
	float world[16];
	float grad1[4];
	float grad2[4];
	float params[4];
	float extra[4];
};
static_assert(sizeof(StarUniforms) == 192);
static_assert(sizeof(StarUniforms) % 16 == 0);

[[nodiscard]] QImage RenderBorderTexture() {
	auto renderer = QSvgRenderer(u":/gui/art/premium/star_texture.svg"_q);
	auto image = QImage(
		kBorderTextureSize,
		kBorderTextureSize,
		QImage::Format_RGBA8888_Premultiplied);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	renderer.render(&p);
	p.end();
	return image;
}

} // namespace

StarRenderer::StarRenderer() = default;

StarRenderer::~StarRenderer() {
	releaseResources();
}

void StarRenderer::setState(State state) {
	_state = state;
}

void StarRenderer::setColors(QColor gradient1, QColor gradient2) {
	_gradient1 = { {
		float(gradient1.redF()),
		float(gradient1.greenF()),
		float(gradient1.blueF()),
	} };
	_gradient2 = { {
		float(gradient2.redF()),
		float(gradient2.greenF()),
		float(gradient2.blueF()),
	} };
}

void StarRenderer::setGolden(bool golden) {
	_golden = golden;
}

void StarRenderer::initialize(
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

	const auto model = LoadStarModel();
	if (model.isNull()) {
		LOG(("PremiumStar: failed to load model"));
		_creationFailed = true;
		return;
	}
	_vertexCount = model.vertexCount;

	_vertexBuffer = rhi->newBuffer(
		QRhiBuffer::Immutable,
		QRhiBuffer::VertexBuffer,
		model.vertices.size() * sizeof(float));
	_vertexBuffer->create();

	_uniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(StarUniforms));
	_uniformBuffer->create();

	const auto border = RenderBorderTexture();
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
		LOG(("PremiumStar: pipeline creation failed, disabling effect"));
		_creationFailed = true;
		releaseResources();
		return;
	}

	auto *rub = rhi->nextResourceUpdateBatch();
	rub->uploadStaticBuffer(_vertexBuffer, model.vertices.data());
	rub->uploadTexture(_borderTexture, border);
	rub->uploadTexture(_flecksTexture, flecks);
	cb->resourceUpdate(rub);

	_initialized = true;

	LOG(("PremiumStar: initialized, vertices=%1 backend=%2 device=%3")
		.arg(_vertexCount)
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

bool StarRenderer::createPipeline(QRhiRenderTarget *rt) {
	const auto vertShader = LoadObject3dShader(u"premium_star.vert"_q);
	const auto fragShader = LoadObject3dShader(u"premium_star.frag"_q);
	if (!vertShader.isValid() || !fragShader.isValid()) {
		return false;
	}

	_srb = _rhi->newShaderResourceBindings();
	_srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_borderTexture,
			_borderSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_flecksTexture,
			_flecksSampler),
	});
	if (!_srb->create()) {
		return false;
	}

	_pipeline = _rhi->newGraphicsPipeline();
	_pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, vertShader },
		{ QRhiShaderStage::Fragment, fragShader },
	});

	QRhiVertexInputLayout inputLayout;
	inputLayout.setBindings({
		{ quint32(kFloatsPerVertex * sizeof(float)) },
	});
	inputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float3, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float) },
		{ 0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float) },
	});
	_pipeline->setVertexInputLayout(inputLayout);

	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	_pipeline->setTargetBlends({ blend });

	_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
	_pipeline->setCullMode(QRhiGraphicsPipeline::None);
	_pipeline->setDepthTest(true);
	_pipeline->setDepthWrite(true);
	_pipeline->setSampleCount(rt->sampleCount());
	_pipeline->setShaderResourceBindings(_srb);
	_pipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
	return _pipeline->create();
}

void StarRenderer::render(
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
	world.translate(0.f, _state.bob, 0.f);
	world.rotate(-_state.pitch, 1.f, 0.f, 0.f);
	world.rotate(-_state.yaw, 0.f, 1.f, 0.f);

	const auto mvp = rhi->clipSpaceCorrMatrix() * projection * view * world;

	auto uni = StarUniforms();
	memcpy(uni.mvp, mvp.constData(), sizeof(uni.mvp));
	memcpy(uni.world, world.constData(), sizeof(uni.world));
	uni.grad1[0] = _gradient1[0];
	uni.grad1[1] = _gradient1[1];
	uni.grad1[2] = _gradient1[2];
	uni.grad1[3] = 1.f;
	uni.grad2[0] = _gradient2[0];
	uni.grad2[1] = _gradient2[1];
	uni.grad2[2] = _gradient2[2];
	uni.grad2[3] = 1.f;
	uni.params[0] = _state.shimmer;
	uni.params[1] = kSpec1;
	uni.params[2] = kSpec2;
	uni.params[3] = kDiffuse;
	uni.extra[0] = kNormalSpec;
	uni.extra[1] = _state.alpha;
	uni.extra[2] = _golden ? 1.f : 0.f;
	uni.extra[3] = 0.f;

	auto *rub = rhi->nextResourceUpdateBatch();
	rub->updateDynamicBuffer(_uniformBuffer, 0, sizeof(uni), &uni);

	cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, rub);
	cb->setGraphicsPipeline(_pipeline);
	cb->setViewport({
		0.f,
		0.f,
		float(pixelSize.width()),
		float(pixelSize.height()) });
	cb->setShaderResources(_srb);
	const QRhiCommandBuffer::VertexInput vbuf[] = { { _vertexBuffer, 0 } };
	cb->setVertexInput(0, 1, vbuf);
	cb->draw(_vertexCount);
	cb->endPass();
}

void StarRenderer::releaseResources() {
	if (!_rhi) {
		return;
	}
	delete _pipeline;
	_pipeline = nullptr;
	delete _srb;
	_srb = nullptr;
	delete _flecksSampler;
	_flecksSampler = nullptr;
	delete _flecksTexture;
	_flecksTexture = nullptr;
	delete _borderSampler;
	_borderSampler = nullptr;
	delete _borderTexture;
	_borderTexture = nullptr;
	delete _uniformBuffer;
	_uniformBuffer = nullptr;
	delete _vertexBuffer;
	_vertexBuffer = nullptr;

	_initialized = false;
	_vertexCount = 0;
	_rhi = nullptr;
}

} // namespace Ui::Premium

#endif // Qt >= 6.7
