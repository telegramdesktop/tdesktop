/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_diamond_renderer.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "ui/effects/premium_3d_mesh.h"
#include "base/debug_log.h"

#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtGui/QMatrix4x4>
#include <rhi/qrhi.h>

namespace Ui::Premium {
namespace {

constexpr auto kBodyFloatsPerVertex = 12;
constexpr auto kSparkleFloatsPerVertex = 8;
constexpr auto kFrameCount = 1440;
constexpr auto kFramesPerSecond = 240.;
constexpr auto kFrameFloats = 42;
constexpr auto kLightFloats = 40;
constexpr auto kHaloScaleFloat = 40;
constexpr auto kPlaneCount = 17;
constexpr auto kAnchorCount = 8;
constexpr auto kFirstSmallSparkleAnchor = 1;
constexpr auto kWidthCount = 91;
constexpr auto kRefraction = 0.72f;
constexpr auto kIdleRotationRate = 0.596f;
constexpr auto kBodyWidth = 336.f / 512.f;
constexpr auto kReferenceHeight = 1.52f;
constexpr auto kPi = float(M_PI);

struct alignas(16) Uniforms {
	float model[16];
	float projection[16];
	float inverseModel[16];
	float clipCorrection[16];
	float parameters[4];
	float viewport[4];
	float lights[kLightFloats];
	float sparkleHalo[4];
	float facetProjection[4];
	float composition[4];
	float planes[kPlaneCount * 4];
	float anchors[kAnchorCount * 8];
};
static_assert(sizeof(Uniforms) == 1024);

[[nodiscard]] std::vector<float> ReadFloats(const QString &name) {
	auto file = QFile(u":/gui/art/premium/diamond/%1.bin"_q.arg(name));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto bytes = file.readAll();
	if (bytes.isEmpty() || (bytes.size() % sizeof(float))) {
		return {};
	}
	auto result = std::vector<float>(bytes.size() / sizeof(float));
	auto stream = QDataStream(bytes);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
	for (auto &value : result) {
		stream >> value;
	}
	return result;
}

[[nodiscard]] float Smooth(float value) {
	value = std::clamp(value, 0.f, 1.f);
	return value * value * (3.f - 2.f * value);
}

} // namespace

DiamondRenderer::DiamondRenderer() = default;

DiamondRenderer::~DiamondRenderer() {
	releaseResources();
}

void DiamondRenderer::setState(State state) {
	_state = state;
}

DiamondRenderer::Data DiamondRenderer::LoadData() {
	auto result = Data{
		.meshes = {
			ReadFloats(u"vertices"_q),
			ReadFloats(u"main"_q),
			ReadFloats(u"small"_q),
		},
		.frames = ReadFloats(u"frames"_q),
		.planes = ReadFloats(u"planes"_q),
		.anchors = ReadFloats(u"anchors"_q),
		.widths = ReadFloats(u"widths"_q),
		.facetProjection = ReadFloats(u"facetProjection"_q),
		.camera = ReadFloats(u"camera"_q),
	};
	const auto valid = !result.meshes[kBodyMesh].empty()
		&& !(result.meshes[kBodyMesh].size() % kBodyFloatsPerVertex)
		&& !result.meshes[kMainSparkleMesh].empty()
		&& !(result.meshes[kMainSparkleMesh].size() % kSparkleFloatsPerVertex)
		&& !result.meshes[kSmallSparkleMesh].empty()
		&& !(result.meshes[kSmallSparkleMesh].size() % kSparkleFloatsPerVertex)
		&& (result.frames.size() == (kFrameCount + 1) * kFrameFloats)
		&& (result.planes.size() == kPlaneCount * 4)
		&& (result.anchors.size() == kAnchorCount * 8)
		&& (result.widths.size() == kWidthCount)
		&& (result.facetProjection.size() == 4)
		&& (result.camera.size() == 3);
	if (!valid) {
		return {};
	}
	return result;
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

	if (_data.camera.empty()) {
		_data = LoadData();
	}
	if (_data.camera.empty()) {
		LOG(("PremiumDiamond: failed to load model"));
		_creationFailed = true;
		return;
	}

	for (auto i = 0; i != kMeshCount; ++i) {
		const auto &mesh = _data.meshes[i];
		_vertexCounts[i] = int(mesh.size()) / ((i == kBodyMesh)
			? kBodyFloatsPerVertex
			: kSparkleFloatsPerVertex);
		_vertexBuffers[i] = rhi->newBuffer(
			QRhiBuffer::Immutable,
			QRhiBuffer::VertexBuffer,
			mesh.size() * sizeof(float));
		if (!_vertexBuffers[i]->create()) {
			LOG(("PremiumDiamond: vertex buffer creation failed"));
			_creationFailed = true;
			releaseResources();
			return;
		}
	}

	_uniforms = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		sizeof(Uniforms));
	if (!_uniforms->create()) {
		_creationFailed = true;
		releaseResources();
		return;
	}

	const auto align = std::max<quint32>(rhi->ubufAlignment(), 1);
	const auto slotBytes = quint32(4 * sizeof(float));
	_sparkleDrawStride = ((slotBytes + align - 1) / align) * align;
	// D3D11 accepts uniform buffers only as Dynamic ones.
	_sparkleDraws = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		kSparkleDraws * _sparkleDrawStride);
	if (!_sparkleDraws->create()) {
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

	const auto slotFloats = _sparkleDrawStride / sizeof(float);
	auto drawData = std::vector<float>(kSparkleDraws * slotFloats, 0.f);
	drawData[slotFloats] = float(kFirstSmallSparkleAnchor);

	auto *rub = rhi->nextResourceUpdateBatch();
	for (auto i = 0; i != kMeshCount; ++i) {
		rub->uploadStaticBuffer(_vertexBuffers[i], _data.meshes[i].data());
	}
	rub->updateDynamicBuffer(
		_sparkleDraws,
		0,
		quint32(drawData.size() * sizeof(float)),
		drawData.data());
	cb->resourceUpdate(rub);

	_initialized = true;

	LOG(("PremiumDiamond: initialized, backend=%1 device=%2")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}

bool DiamondRenderer::createPipelines(QRhiRenderTarget *rt) {
	const auto bodyVertex = LoadObject3dShader(u"premium_diamond.vert"_q);
	const auto bodyFragment = LoadObject3dShader(u"premium_diamond.frag"_q);
	const auto sparkleVertex = LoadObject3dShader(
		u"premium_diamond_sparkle.vert"_q);
	const auto sparkleFragment = LoadObject3dShader(
		u"premium_diamond_sparkle.frag"_q);
	if (!bodyVertex.isValid()
		|| !bodyFragment.isValid()
		|| !sparkleVertex.isValid()
		|| !sparkleFragment.isValid()) {
		return false;
	}

	_bodySrb = _rhi->newShaderResourceBindings();
	_bodySrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniforms),
	});
	if (!_bodySrb->create()) {
		return false;
	}
	for (auto i = 0; i != kSparkleDraws; ++i) {
		_sparkleSrb[i] = _rhi->newShaderResourceBindings();
		_sparkleSrb[i]->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage,
				_uniforms),
			QRhiShaderResourceBinding::uniformBuffer(
				1,
				QRhiShaderResourceBinding::VertexStage,
				_sparkleDraws,
				i * _sparkleDrawStride,
				quint32(4 * sizeof(float))),
		});
		if (!_sparkleSrb[i]->create()) {
			return false;
		}
	}

	auto bodyLayout = QRhiVertexInputLayout();
	bodyLayout.setBindings({
		{ quint32(kBodyFloatsPerVertex * sizeof(float)) },
	});
	bodyLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float4, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float4, 4 * sizeof(float) },
		{ 0, 2, QRhiVertexInputAttribute::Float4, 8 * sizeof(float) },
	});
	auto sparkleLayout = QRhiVertexInputLayout();
	sparkleLayout.setBindings({
		{ quint32(kSparkleFloatsPerVertex * sizeof(float)) },
	});
	sparkleLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float4, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float4, 4 * sizeof(float) },
	});

	_bodyPipeline = _rhi->newGraphicsPipeline();
	_bodyPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, bodyVertex },
		{ QRhiShaderStage::Fragment, bodyFragment },
	});
	_bodyPipeline->setVertexInputLayout(bodyLayout);
	_bodyPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
	_bodyPipeline->setCullMode(QRhiGraphicsPipeline::Back);
	_bodyPipeline->setDepthTest(true);
	_bodyPipeline->setDepthWrite(true);
	_bodyPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
	_bodyPipeline->setSampleCount(rt->sampleCount());
	_bodyPipeline->setShaderResourceBindings(_bodySrb);
	_bodyPipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
	if (!_bodyPipeline->create()) {
		return false;
	}

	// Constant alpha fades sparkles and body as one premultiplied group.
	auto blend = QRhiGraphicsPipeline::TargetBlend();
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::ConstantAlpha;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::ConstantAlpha;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

	_sparklePipeline = _rhi->newGraphicsPipeline();
	_sparklePipeline->setFlags(QRhiGraphicsPipeline::UsesBlendConstants);
	_sparklePipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, sparkleVertex },
		{ QRhiShaderStage::Fragment, sparkleFragment },
	});
	_sparklePipeline->setVertexInputLayout(sparkleLayout);
	_sparklePipeline->setTargetBlends({ blend });
	_sparklePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
	_sparklePipeline->setCullMode(QRhiGraphicsPipeline::None);
	_sparklePipeline->setDepthTest(false);
	_sparklePipeline->setDepthWrite(false);
	_sparklePipeline->setSampleCount(rt->sampleCount());
	_sparklePipeline->setShaderResourceBindings(_sparkleSrb[0]);
	_sparklePipeline->setRenderPassDescriptor(rt->renderPassDescriptor());
	return _sparklePipeline->create();
}

float DiamondRenderer::horizontalScale(float yaw, float pitch) const {
	const auto &widths = _data.widths;
	const auto quadrant = std::abs(std::remainder(yaw, kPi / 2.f));
	const auto sample = std::min(90.f, quadrant * 360.f / kPi);
	const auto i = std::min(89, int(sample));
	const auto t = sample - i;
	const auto a = widths[i];
	const auto b = widths[i + 1];
	const auto da = (i == 0) ? 0.f : (b - widths[i - 1]) / 2.f;
	const auto db = (i == 89) ? 0.f : (widths[i + 2] - a) / 2.f;
	const auto width = a + t * (da + t * (3.f * (b - a) - 2.f * da - db
		+ t * (2.f * (a - b) + da + db)));
	const auto turn = std::sin(2.f * quadrant);
	const auto target = widths[0] * (1.f - 0.035f * turn * turn);
	const auto tilt = (std::abs(std::sin(pitch)) - std::sin(0.25f))
		/ (std::sin(0.96f) - std::sin(0.25f));
	return 1.f + (target / width - 1.f) * (1.f - Smooth(tilt));
}

void DiamondRenderer::updateRotationRate(float yaw, float pitch) {
	const auto dt = _state.time - _lastTime;
	if (_hasLast && dt > 0.f && dt < 1.f) {
		const auto delta = std::atan2(
			std::sin(yaw - _lastYaw),
			std::cos(yaw - _lastYaw));
		_rotationRate = std::hypot(delta, pitch - _lastPitch) / dt;
	} else if (!_hasLast) {
		_rotationRate = kIdleRotationRate;
	}
	_lastTime = _state.time;
	_lastYaw = yaw;
	_lastPitch = pitch;
	_hasLast = true;
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
	const auto &camera = _data.camera;
	const auto yaw = _state.yaw * kPi / 180.f;
	const auto pitch = _state.pitch * kPi / 180.f + camera[0];
	updateRotationRate(yaw, pitch);

	auto uniforms = Uniforms();

	const auto sample = std::fmod(
		double(_state.time) * kFramesPerSecond,
		double(kFrameCount));
	const auto index = std::clamp(int(sample), 0, kFrameCount - 1);
	const auto fraction = float(sample - index);
	const auto from = _data.frames.data() + index * kFrameFloats;
	const auto till = from + kFrameFloats;
	for (auto i = 0; i != kLightFloats; ++i) {
		uniforms.lights[i] = from[i] + (till[i] - from[i]) * fraction;
	}
	const auto haloScale = from[kHaloScaleFloat]
		+ (till[kHaloScaleFloat] - from[kHaloScaleFloat]) * fraction;

	const auto horizontal = horizontalScale(yaw, pitch);

	auto model = QMatrix4x4();
	model.rotate(pitch * 180.f / kPi, 1.f, 0.f, 0.f);
	model.rotate(yaw * 180.f / kPi, 0.f, 1.f, 0.f);
	memcpy(uniforms.model, model.constData(), sizeof(uniforms.model));
	const auto inverse = model.transposed();
	memcpy(
		uniforms.inverseModel,
		inverse.constData(),
		sizeof(uniforms.inverseModel));
	const auto correction = rhi->clipSpaceCorrMatrix();
	memcpy(
		uniforms.clipCorrection,
		correction.constData(),
		sizeof(uniforms.clipCorrection));

	const auto aspect = float(pixelSize.width()) / pixelSize.height();
	const auto zoom = kBodyWidth * kReferenceHeight / _data.widths[0];
	const auto h = kReferenceHeight / zoom * std::max(1.f, 1.f / aspect);
	const auto w0 = 1.f / camera[2];
	const auto wz = -w0 / camera[1];
	auto &projection = uniforms.projection;
	projection[0] = horizontal / (h * aspect);
	projection[5] = 1.f / h;
	projection[9] = 0.12f / h * wz;
	projection[10] = -w0 / 6.f;
	projection[11] = wz;
	projection[13] = 0.12f / h * w0;
	projection[14] = 0.5f * w0;
	projection[15] = w0;

	auto best = -1.f;
	auto angle = 0.f;
	for (auto n = 0; n != 4; ++n) {
		const auto a = n * kPi / 2.f;
		const auto facing = std::cos(yaw + a) * std::cos(pitch - camera[0]);
		if (facing > best) {
			best = facing;
			angle = a;
		}
	}
	const auto visibility = Smooth(
		1.f - std::acos(std::clamp(best, -1.f, 1.f)) / (kPi / 15.f))
		* Smooth(((2.f * kPi / 15.f) / std::max(_rotationRate, 0.001f)
			- 0.06f) / 0.34f);

	uniforms.parameters[0] = _state.time;
	uniforms.parameters[1] = kRefraction;
	uniforms.parameters[2] = 1.f;
	uniforms.parameters[3] = 1.f;
	uniforms.viewport[0] = float(pixelSize.width());
	uniforms.viewport[1] = float(pixelSize.height());
	uniforms.viewport[2] = float(kPlaneCount);
	uniforms.sparkleHalo[0] = haloScale;
	uniforms.sparkleHalo[1] = angle;
	uniforms.sparkleHalo[2] = horizontal;
	uniforms.sparkleHalo[3] = visibility;
	memcpy(
		uniforms.facetProjection,
		_data.facetProjection.data(),
		sizeof(uniforms.facetProjection));
	uniforms.composition[0] = _state.alpha;
	memcpy(uniforms.planes, _data.planes.data(), sizeof(uniforms.planes));
	memcpy(uniforms.anchors, _data.anchors.data(), sizeof(uniforms.anchors));

	auto *rub = rhi->nextResourceUpdateBatch();
	rub->updateDynamicBuffer(_uniforms, 0, sizeof(uniforms), &uniforms);

	cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, rub);
	const auto viewport = QRhiViewport(
		0.f,
		0.f,
		float(pixelSize.width()),
		float(pixelSize.height()));

	cb->setGraphicsPipeline(_bodyPipeline);
	cb->setViewport(viewport);
	cb->setShaderResources(_bodySrb);
	const QRhiCommandBuffer::VertexInput body[] = {
		{ _vertexBuffers[kBodyMesh], 0 },
	};
	cb->setVertexInput(0, 1, body);
	cb->draw(_vertexCounts[kBodyMesh]);

	cb->setGraphicsPipeline(_sparklePipeline);
	cb->setViewport(viewport);
	// D3D11 has no constant alpha factor, QRhi gives it the whole constant,
	// so the color channels carry the same alpha.
	const auto alpha = _state.alpha;
	cb->setBlendConstants(QColor::fromRgbF(alpha, alpha, alpha, alpha));
	for (auto i = 0; i != kSparkleDraws; ++i) {
		const auto mesh = (i == 0) ? kMainSparkleMesh : kSmallSparkleMesh;
		cb->setShaderResources(_sparkleSrb[i]);
		const QRhiCommandBuffer::VertexInput sparkle[] = {
			{ _vertexBuffers[mesh], 0 },
		};
		cb->setVertexInput(0, 1, sparkle);
		cb->draw(
			_vertexCounts[mesh],
			(i == 0) ? 1 : (kAnchorCount - kFirstSmallSparkleAnchor));
	}
	cb->endPass();
}

void DiamondRenderer::releaseResources() {
	if (!_rhi) {
		return;
	}
	delete _bodyPipeline;
	_bodyPipeline = nullptr;
	delete _sparklePipeline;
	_sparklePipeline = nullptr;
	delete _bodySrb;
	_bodySrb = nullptr;
	for (auto &srb : _sparkleSrb) {
		delete srb;
		srb = nullptr;
	}
	delete _sparkleDraws;
	_sparkleDraws = nullptr;
	delete _uniforms;
	_uniforms = nullptr;
	for (auto i = 0; i != kMeshCount; ++i) {
		delete _vertexBuffers[i];
		_vertexBuffers[i] = nullptr;
		_vertexCounts[i] = 0;
	}
	_hasLast = false;

	_initialized = false;
	_rhi = nullptr;
}

} // namespace Ui::Premium

#endif // Qt >= 6.7 && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
