/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rhi/rhi_renderer.h"
#include "ui/gl/gl_surface.h"

#include <QImage>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiGraphicsPipeline;
class QRhiComputePipeline;
class QRhiShaderResourceBindings;
class QRhiRenderTarget;
class QRhiCommandBuffer;

namespace Ui {

struct ThanosItem {
	QImage snapshot;
	QRectF rect;
};

class ThanosEffectRenderer final
	: public GL::Renderer
	, public Rhi::Renderer {
public:
	explicit ThanosEffectRenderer(
		rpl::producer<float64> devicePixelRatio);
	~ThanosEffectRenderer();

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

	void addItem(ThanosItem item);
	[[nodiscard]] bool hasActiveItems() const;

	rpl::producer<> allDone() const;

private:
	struct AnimatingItem {
		QRhiTexture *texture = nullptr;
		QRhiSampler *sampler = nullptr;
		QRhiTexture *particleStateTexture = nullptr;
		QRhiTexture *particleVelocityTexture = nullptr;
		QRhiSampler *particleStateSampler = nullptr;
		QRhiBuffer *computeInitUniformBuffer = nullptr;
		QRhiBuffer *computeUpdateUniformBuffer = nullptr;
		QRhiBuffer *renderUniformBuffer = nullptr;
		QRhiShaderResourceBindings *computeInitSrb = nullptr;
		QRhiShaderResourceBindings *computeUpdateSrb = nullptr;
		QRhiShaderResourceBindings *renderSrb = nullptr;
		QImage uploadImage;
		QRectF rect;
		uint32_t particleCountX = 0;
		uint32_t particleCountY = 0;
		float64 phase = 0.;
		bool particlesInitialized = false;
		bool needsInitDispatch = false;
	};

	[[nodiscard]] bool createPipelines(QRhiRenderTarget *rt);
	void addPendingItems(QRhiCommandBuffer *cb);
	AnimatingItem createAnimatingItem(ThanosItem &&item);
	void destroyAnimatingItem(AnimatingItem &item);

	QRhi *_rhi = nullptr;
	float64 _factor = 1.;

	QRhiBuffer *_quadVertexBuffer = nullptr;
	QRhiBuffer *_computeInitUniformBuffer = nullptr;
	QRhiBuffer *_computeUpdateUniformBuffer = nullptr;
	QRhiBuffer *_renderUniformBuffer = nullptr;

	QRhiTexture *_placeholderTexture = nullptr;
	QRhiSampler *_placeholderSampler = nullptr;
	QRhiTexture *_placeholderStateTexture = nullptr;
	QRhiSampler *_placeholderStateSampler = nullptr;

	QRhiShaderResourceBindings *_computeInitSrbLayout = nullptr;
	QRhiShaderResourceBindings *_computeUpdateSrbLayout = nullptr;
	QRhiShaderResourceBindings *_renderSrbLayout = nullptr;

	QRhiComputePipeline *_computeInitPipeline = nullptr;
	QRhiComputePipeline *_computeUpdatePipeline = nullptr;
	QRhiGraphicsPipeline *_renderPipeline = nullptr;

	std::vector<AnimatingItem> _items;
	std::vector<ThanosItem> _pendingItems;

	crl::time _lastFrameTime = 0;
	bool _initialized = false;
	bool _creationFailed = false;
	uint32_t _seedCounter = 0;

	rpl::event_stream<> _allDone;
	rpl::lifetime _lifetime;

};

} // namespace Ui

#endif // Qt >= 6.7
