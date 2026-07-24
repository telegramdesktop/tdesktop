/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rhi/rhi_renderer.h"
#include "ui/gl/gl_surface.h"

#include <QtGui/QColor>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiRenderTarget;
class QRhiCommandBuffer;

namespace Ui::Premium {

class StarRenderer final
	: public GL::Renderer
	, public Rhi::Renderer {
public:
	StarRenderer();
	~StarRenderer();

	struct State {
		float yaw = 0.;
		float pitch = 0.;
		float bob = 0.;
		float shimmer = 0.;
		float alpha = 1.;
	};
	void setState(State state);
	void setColors(QColor gradient1, QColor gradient2);
	void setGolden(bool golden);

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
	[[nodiscard]] bool createPipeline(QRhiRenderTarget *rt);

	QRhi *_rhi = nullptr;

	QRhiBuffer *_vertexBuffer = nullptr;
	QRhiBuffer *_uniformBuffer = nullptr;
	QRhiTexture *_borderTexture = nullptr;
	QRhiSampler *_borderSampler = nullptr;
	QRhiTexture *_flecksTexture = nullptr;
	QRhiSampler *_flecksSampler = nullptr;
	QRhiShaderResourceBindings *_srb = nullptr;
	QRhiGraphicsPipeline *_pipeline = nullptr;

	int _vertexCount = 0;
	State _state;
	bool _golden = false;
	std::array<float, 3> _gradient1 = { { 1.f, 1.f, 1.f } };
	std::array<float, 3> _gradient2 = { { 0.8902f, 0.9255f, 0.9804f } };

	bool _initialized = false;
	bool _creationFailed = false;

};

} // namespace Ui::Premium

#endif // Qt >= 6.7
