/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_pip_renderer.h"
#include "ui/rhi/rhi_renderer.h"
#include "ui/rhi/rhi_image.h"
#include "ui/gl/gl_math.h"

#ifdef Q_OS_MAC
#include "media/view/media_view_metal_texture.h"
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiResourceUpdateBatch;

namespace Media::View {

class Pip::RendererRhi final
	: public Pip::Renderer
	, public Ui::Rhi::Renderer {
public:
	explicit RendererRhi(not_null<Pip*> owner);
	~RendererRhi();

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
	struct Control {
		int index = -1;
		not_null<const style::icon*> icon;
		not_null<const style::icon*> iconOver;
	};

	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) override;
	void paintRadialLoading(
		QRect inner,
		float64 controlsShown) override;
	void paintButtonsStart() override;
	void paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) override;
	void paintPlayback(QRect outer, float64 shown) override;
	void paintVolumeController(QRect outer, float64 shown) override;

	void paintTransformedContent(
		QRhiGraphicsPipeline *pipeline,
		QRhiShaderResourceBindings *srb,
		ContentGeometry geometry,
		int slot);
	void paintUsingRaster(
		Ui::Rhi::Image &image,
		QRect rect,
		Fn<void(QPainter&&)> method,
		bool transparent = false);
	[[nodiscard]] int allocateDrawSlot();
	[[nodiscard]] QRhiShaderResourceBindings *allocateSrb();

	struct DrawCommand {
		QRhiGraphicsPipeline *pipeline = nullptr;
		QRhiShaderResourceBindings *srb = nullptr;
		QRhiBuffer *vertexBuffer = nullptr;
		quint32 vertexOffset = 0;
	};

	void createShadowTexture();
	void createPipelines();
	void validateControls();
	void invalidateControls();

	[[nodiscard]] static QRect RoundingRect(ContentGeometry geometry);
	[[nodiscard]] static Control ControlMeta(
		OverState control,
		int index = 0);

	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(const QRectF &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(
		const Ui::GL::Rect &raster) const;

	const not_null<Pip*> _owner;

	QRhi *_rhi = nullptr;
	QRhiRenderTarget *_rt = nullptr;
	QRhiCommandBuffer *_cb = nullptr;
	QRhiResourceUpdateBatch *_rub = nullptr;
	QSize _viewport;
	float _factor = 1.;
	int _ifactor = 1;

	QRhiBuffer *_vertexBuffer = nullptr;
	QRhiBuffer *_uniformBuffer = nullptr;
	QRhiSampler *_sampler = nullptr;

	QRhiTexture *_placeholderTexture = nullptr;
	QRhiTexture *_rgbaTexture = nullptr;
	QSize _rgbaSize;
	quint64 _cacheKey = 0;
	int _trackFrameIndex = -1;
	bool _chromaNV12 = false;
	bool _usingExternalVideoTextures = false;

	QRhiTexture *_yTexture = nullptr;
	QRhiTexture *_uTexture = nullptr;
	QRhiTexture *_vTexture = nullptr;
	QRhiTexture *_uvTexture = nullptr;
	QSize _lumaSize;
	QSize _chromaSize;

	QRhiGraphicsPipeline *_argb32Pipeline = nullptr;
	QRhiGraphicsPipeline *_yuv420Pipeline = nullptr;
	QRhiGraphicsPipeline *_nv12Pipeline = nullptr;
	QRhiGraphicsPipeline *_imagePipeline = nullptr;
	QRhiGraphicsPipeline *_imageBlendPipeline = nullptr;
	QRhiGraphicsPipeline *_controlsPipeline = nullptr;

	// Layout-only SRBs: used solely as pipeline layout templates,
	// not for actual draw calls. Per-draw SRBs come from _srbPool.
	QRhiShaderResourceBindings *_argb32Srb = nullptr;
	QRhiShaderResourceBindings *_yuv420Srb = nullptr;
	QRhiShaderResourceBindings *_nv12Srb = nullptr;
	QRhiShaderResourceBindings *_imageSrb = nullptr;
	QRhiShaderResourceBindings *_controlsSrb = nullptr;

	std::vector<QRhiShaderResourceBindings*> _srbPool;
	int _nextSrbIndex = 0;

	Ui::Rhi::Image _shadowImage;
	Ui::Rhi::Image _radialImage;
	Ui::Rhi::Image _controlsImage;
	Ui::Rhi::Image _playbackImage;
	Ui::Rhi::Image _volumeControllerImage;

	static constexpr auto kControlsCount = 7;
	static constexpr auto kMaxDraws = 12;
	static constexpr auto kVertexSlotSize = 4 * 6 * sizeof(float);
	std::array<QRect, kControlsCount * 2> _controlsTextures;
	std::vector<DrawCommand> _drawCommands;
	int _nextDrawSlot = 0;

	bool _initialized = false;

#ifdef Q_OS_MAC
	MetalTextureCache _metalTextureCache;
#endif

	rpl::lifetime _lifetime;

};

} // namespace Media::View

#endif // Qt >= 6.7
