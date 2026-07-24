/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_renderer.h"
#include "ui/rhi/rhi_renderer.h"
#include "ui/rhi/rhi_image.h"
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

class VideoStream;

class OverlayWidget::RendererRhi final
	: public OverlayWidget::Renderer
	, public Ui::Rhi::Renderer
	, public base::has_weak_ptr {
public:
	explicit RendererRhi(not_null<OverlayWidget*> owner);
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

	QColor rhiClearColor() override;
	std::optional<QColor> clearColor() override;

private:
	void paintBackground() override;
	void paintVideoStream() override;
	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground,
		int index = 0) override;
	void paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) override;
	void paintThemePreview(QRect outer) override;
	void paintDocumentBubble(QRect outer, QRect icon) override;
	void paintSaveMsg(QRect outer) override;
	void paintChapter(QRect outer) override;
	void paintSpeedBoost(QRect outer) override;
	void paintControlsStart() override;
	void paintControl(
		Over control,
		QRect over,
		float64 overOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) override;
	void paintFooter(QRect outer, float64 opacity) override;
	void paintCaption(QRect outer, float64 opacity) override;
	void paintGroupThumbs(QRect outer, float64 opacity) override;
	void paintRoundedCorners(int radius) override;
	void paintStoriesSiblingPart(
		int index,
		const QImage &image,
		QRect rect,
		float64 opacity = 1.) override;

	void paintRecognitionOverlay(
		const QImage &image,
		ContentGeometry geometry);

	struct Control {
		int index = -1;
		not_null<const style::icon*> icon;
	};

	void createPipelines();
	void validateControlsFade();
	void validateControls();
	void invalidateControls();
	[[nodiscard]] Control controlMeta(Over control) const;

	void drawTexturedQuad(
		QRhiGraphicsPipeline *pipeline,
		QRhiTexture *texture,
		const float *coords,
		float opacity = 1.f,
		bool blend = false);

	void drawContentQuad(
		QRhiTexture *contentTexture,
		const float *coords,
		ContentGeometry geometry,
		bool fillTransparentBackground,
		bool blend);

	void fillShadowUniforms(
		float *shadowTopRect,
		float *shadowBottomSkipOpacityFullFade,
		ContentGeometry geometry) const;

	void paintUsingRaster(
		QRect rect,
		Fn<void(Painter&)> method,
		bool transparent = false,
		float opacity = 1.f);

	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(const QRectF &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(
		const Ui::GL::Rect &raster) const;
	[[nodiscard]] Ui::GL::Rect scaleRect(
		const Ui::GL::Rect &unscaled,
		float64 scale) const;

	const not_null<OverlayWidget*> _owner;

	QRhi *_rhi = nullptr;
	QRhiRenderTarget *_rt = nullptr;
	QRhiCommandBuffer *_cb = nullptr;
	QRhiResourceUpdateBatch *_rub = nullptr;
	QSize _viewport;
	float _factor = 1.;
	int _ifactor = 1;

	static constexpr int kMaxDraws = 32;
	static constexpr int kVertexSize = 4 * 4 * sizeof(float);

	QRhiBuffer *_vertexBuffer = nullptr;
	QRhiBuffer *_fillVertexBuffer = nullptr;
	QRhiBuffer *_uniformBuffer = nullptr;
	QRhiSampler *_sampler = nullptr;
	QRhiTexture *_placeholderTexture = nullptr;

	QRhiGraphicsPipeline *_imagePipeline = nullptr;
	QRhiGraphicsPipeline *_imageBlendPipeline = nullptr;
	QRhiGraphicsPipeline *_staticContentPipeline = nullptr;
	QRhiGraphicsPipeline *_staticContentBlendPipeline = nullptr;
	QRhiGraphicsPipeline *_transparentContentPipeline = nullptr;
	QRhiGraphicsPipeline *_roundedCornersPipeline = nullptr;
	QRhiGraphicsPipeline *_yuv420Pipeline = nullptr;
	QRhiGraphicsPipeline *_yuv420BlendPipeline = nullptr;
	QRhiGraphicsPipeline *_nv12Pipeline = nullptr;
	QRhiGraphicsPipeline *_nv12BlendPipeline = nullptr;

	struct DrawCommand {
		QRhiGraphicsPipeline *pipeline = nullptr;
		QRhiShaderResourceBindings *srb = nullptr;
		int vertexIndex = 0;
		bool fillVertex = false;
	};
	std::vector<DrawCommand> _drawCommands;
	std::vector<QRhiShaderResourceBindings*> _perDrawSrbs;
	int _nextVertexSlot = 0;

	QRhiTexture *_rgbaTextures[3] = {};
	QSize _rgbaSizes[3];
	quint64 _cacheKeys[3] = {};

	QRhiTexture *_yTexture = nullptr;
	QRhiTexture *_uTexture = nullptr;
	QRhiTexture *_vTexture = nullptr;
	QRhiTexture *_uvTexture = nullptr;
	QSize _lumaSize;
	QSize _chromaSize;
	bool _chromaNV12 = false;
	bool _usingExternalVideoTextures = false;
	int _trackFrameIndex = 0;
	int _streamedIndex = 0;

	struct PoolTexture {
		QRhiTexture *texture = nullptr;
		QSize size;
	};
	std::vector<PoolTexture> _texturePool;
	int _nextPoolIndex = 0;
	[[nodiscard]] QRhiTexture *acquirePoolTexture(QSize size);

	static constexpr auto kControlsCount = 8;
	QRhiTexture *_controlsAtlasTexture = nullptr;
	QSize _controlsAtlasSize;
	std::array<QRect, kControlsCount + 1> _controlsTextures;

	static constexpr auto kStoriesSiblingPartsCount = 4;
	QRhiTexture *_storiesSiblingTextures[kStoriesSiblingPartsCount] = {};
	QSize _storiesSiblingSizes[kStoriesSiblingPartsCount];
	quint64 _storiesSiblingCacheKeys[kStoriesSiblingPartsCount] = {};

	QRhiTexture *_controlsFadeTexture = nullptr;
	QSize _controlsFadeSize;
	bool _shadowTopFlip = false;
	bool _shadowsForStories = false;

#ifdef Q_OS_MAC
	MetalTextureCache _metalTextureCache;
#endif

	VideoStream *_pendingVideoStream = nullptr;
	bool _initialized = false;

	rpl::lifetime _storiesLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Media::View

#endif // Qt >= 6.7
