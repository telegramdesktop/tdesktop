/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rhi/rhi_renderer.h"
#include "ui/gl/gl_surface.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiRenderTarget;
class QRhiCommandBuffer;

namespace Ui::Premium {

class DiamondRenderer final
	: public GL::Renderer
	, public Rhi::Renderer {
public:
	DiamondRenderer();
	~DiamondRenderer();

	struct State {
		float yaw = 0.f;
		float pitch = 0.f;
		float time = 0.f;
		float alpha = 1.f;
	};
	void setState(State state);

	void initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void releaseResources() override;

	QColor rhiClearColor() override {
		return QColor(0, 0, 0, 0);
	}

	std::optional<QColor> clearColor() override {
		return QColor(0, 0, 0, 0);
	}

private:
	enum Mesh {
		kBodyMesh,
		kMainSparkleMesh,
		kSmallSparkleMesh,
		kMeshCount,
	};
	static constexpr auto kSparkleDraws = 2;

	struct Data {
		std::array<std::vector<float>, kMeshCount> meshes;
		std::vector<float> frames;
		std::vector<float> planes;
		std::vector<float> anchors;
		std::vector<float> widths;
		std::vector<float> facetProjection;
		std::vector<float> camera;
	};
	[[nodiscard]] static Data LoadData();

	[[nodiscard]] bool createPipelines(QRhiRenderTarget *rt);
	[[nodiscard]] float horizontalScale(float yaw, float pitch) const;
	void updateRotationRate(float yaw, float pitch);

	QRhi *_rhi = nullptr;

	Data _data;
	std::array<QRhiBuffer*, kMeshCount> _vertexBuffers = { {} };
	std::array<int, kMeshCount> _vertexCounts = { {} };
	QRhiBuffer *_uniforms = nullptr;
	QRhiBuffer *_sparkleDraws = nullptr;
	quint32 _sparkleDrawStride = 0;
	QRhiShaderResourceBindings *_bodySrb = nullptr;
	std::array<QRhiShaderResourceBindings*, kSparkleDraws> _sparkleSrb
		= { {} };
	QRhiGraphicsPipeline *_bodyPipeline = nullptr;
	QRhiGraphicsPipeline *_sparklePipeline = nullptr;

	State _state;
	float _lastTime = 0.f;
	float _lastYaw = 0.f;
	float _lastPitch = 0.f;
	float _rotationRate = 0.f;
	bool _hasLast = false;

	bool _initialized = false;
	bool _creationFailed = false;

};

} // namespace Ui::Premium

#endif // Qt >= 6.7 && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
