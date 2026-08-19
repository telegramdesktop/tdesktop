/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rhi/rhi_renderer.h"
#include "ui/gl/gl_surface.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

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
		bool night = false;
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
	static constexpr auto kShellCount = 3;
	static constexpr auto kDrawCount = 5;

	struct DrawSlot {
		int shell = 0;
		bool behind = false;
	};
	static constexpr std::array<DrawSlot, kDrawCount> kDrawSlots = { {
		{ 0, true },
		{ 1, true },
		{ 2, false },
		{ 1, false },
		{ 0, false },
	} };

	[[nodiscard]] bool createPipelines(QRhiRenderTarget *rt);

	QRhi *_rhi = nullptr;

	std::array<QRhiBuffer*, kShellCount> _vertexBuffers = { {} };
	std::array<int, kShellCount> _vertexCounts = { {} };
	QRhiBuffer *_sharedUniform = nullptr;
	QRhiBuffer *_drawUniform = nullptr;
	quint32 _drawStride = 0;
	std::array<QRhiShaderResourceBindings*, kDrawCount> _srb = { {} };
	QRhiGraphicsPipeline *_behindPipeline = nullptr;
	QRhiGraphicsPipeline *_frontPipeline = nullptr;

	State _state;

	bool _initialized = false;
	bool _creationFailed = false;

};

} // namespace Ui::Premium

#endif // Qt >= 6.7
