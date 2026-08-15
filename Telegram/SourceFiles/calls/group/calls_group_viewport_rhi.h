/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/gl/gl_surface.h"
#include "ui/rhi/rhi_renderer.h"
#include "ui/rhi/rhi_image.h"
#include "ui/round_rect.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)

class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiResourceUpdateBatch;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;

namespace Webrtc {
struct FrameWithInfo;
} // namespace Webrtc

namespace Calls::Group {

class Viewport::RendererRhi final
	: public Ui::GL::Renderer
	, public Ui::Rhi::Renderer {
public:
	explicit RendererRhi(not_null<Viewport*> owner);
	~RendererRhi();

	void initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void renderOffscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void renderOnscreen(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) override;
	void releaseResources() override;

	QColor rhiClearColor() override;

	std::optional<QColor> clearColor() override;

private:
	struct TileData {
		quintptr id = 0;
		not_null<PeerData*> peer;
		Ui::Animations::Simple outlined;
		Ui::Animations::Simple paused;
		QImage userpicFrame;
		QRect nameRect;
		int nameVersion = 0;
		mutable int trackIndex = -1;

		QRhiTexture *rgbaTexture = nullptr;
		QSize rgbaSize;

		QRhiTexture *yTexture = nullptr;
		QRhiTexture *uTexture = nullptr;
		QRhiTexture *vTexture = nullptr;
		QSize lumaSize;
		QSize chromaSize;

		QRhiTexture *convertedTexture = nullptr;
		QRhiTextureRenderTarget *convertedRt = nullptr;
		QRhiRenderPassDescriptor *convertedRpDesc = nullptr;
		QSize convertedSize;

		QRhiTexture *downscaleTexture = nullptr;
		QRhiTextureRenderTarget *downscaleRt = nullptr;
		QRhiRenderPassDescriptor *downscaleRpDesc = nullptr;

		QRhiTexture *blurHTexture = nullptr;
		QRhiTextureRenderTarget *blurHRt = nullptr;
		QRhiRenderPassDescriptor *blurHRpDesc = nullptr;

		QRhiTexture *blurVTexture = nullptr;
		QRhiTextureRenderTarget *blurVRt = nullptr;
		QRhiRenderPassDescriptor *blurVRpDesc = nullptr;

		QSize blurSize;
		bool stale = false;
		bool pause = false;
		bool outline = false;
	};

	struct PoolTexture {
		QRhiTexture *texture = nullptr;
		QSize size;
	};

	void createPipelines();
	void ensureNoiseTexture();
	void validateDatas();
	void validateOutlineAnimation(
		not_null<VideoTile*> tile,
		TileData &data);
	void validatePausedAnimation(
		not_null<VideoTile*> tile,
		TileData &data);
	void validateUserpicFrame(
		not_null<VideoTile*> tile,
		TileData &data);
	void ensureButtonsImage();

	void paintTileOffscreen(
		not_null<VideoTile*> tile,
		TileData &tileData);
	void paintTileOnscreen(
		not_null<VideoTile*> tile,
		TileData &tileData,
		float pw,
		float ph);
	void collectOnscreenDraws();
	void issueOnscreenDraws();
	void uploadFrame(
		const Webrtc::FrameWithInfo &data,
		TileData &tileData);
	void drawYuv2RgbPass(
		TileData &tileData,
		QSize frameSize);
	void prepareOffscreenTargets(
		TileData &tileData,
		QSize blurSize);
	void drawDownscalePass(
		TileData &tileData,
		QSize blurSize);
	void drawBlurPass(
		TileData &tileData,
		QSize blurSize);
	void drawFramePass(
		not_null<VideoTile*> tile,
		TileData &tileData,
		QSize blurSize);
	void drawControls(
		not_null<VideoTile*> tile,
		TileData &tileData);

	void paintUsingRaster(
		QRect rect,
		Fn<void(Painter&)> method,
		float opacity = 1.f);

	[[nodiscard]] QRhiTexture *acquirePoolTexture(QSize size);
	[[nodiscard]] Ui::GL::Rect transformRect(const QRect &raster) const;
	[[nodiscard]] Ui::GL::Rect transformRect(
		const Ui::GL::Rect &raster) const;

	[[nodiscard]] bool isExpanded(
		not_null<VideoTile*> tile,
		QSize unscaled,
		QSize tileSize) const;
	[[nodiscard]] float64 countExpandRatio(
		not_null<VideoTile*> tile,
		QSize unscaled,
		const TileAnimation &animation) const;

	const not_null<Viewport*> _owner;

	QRhi *_rhi = nullptr;
	QRhiRenderTarget *_rt = nullptr;
	QRhiCommandBuffer *_cb = nullptr;
	QRhiResourceUpdateBatch *_rub = nullptr;
	QSize _viewport;
	float _factor = 1.f;
	int _ifactor = 1;
	bool _rgbaFrame = false;
	bool _userpicFrame = false;

	static constexpr int kMaxOnscreenDraws = 64;
	static constexpr int kOnscreenVertexSlot = 128;
	static constexpr int kUniformSlot = 256;

	struct OnscreenDraw {
		QRhiGraphicsPipeline *pipeline = nullptr;
		QRhiShaderResourceBindings *srb = nullptr;
		int vertexOffset = 0;
	};
	std::vector<OnscreenDraw> _onscreenDraws;
	int _nextOnscreenSlot = 0;

	QRhiBuffer *_offscreenVertexBuffer = nullptr;
	QRhiBuffer *_onscreenVertexBuffer = nullptr;
	QRhiBuffer *_uniformBuffer = nullptr;
	QRhiSampler *_linearSampler = nullptr;
	QRhiSampler *_nearestSampler = nullptr;
	QRhiSampler *_noiseRepeatSampler = nullptr;
	QRhiTexture *_placeholderTexture = nullptr;

	QRhiGraphicsPipeline *_downscaleArgb32Pipeline = nullptr;
	QRhiGraphicsPipeline *_downscaleYuv420Pipeline = nullptr;
	QRhiGraphicsPipeline *_blurHPipeline = nullptr;
	QRhiGraphicsPipeline *_blurVPipeline = nullptr;
	QRhiGraphicsPipeline *_framePipeline = nullptr;
	QRhiGraphicsPipeline *_controlsPipeline = nullptr;

	QRhiShaderResourceBindings *_downscaleArgb32Srb = nullptr;
	QRhiShaderResourceBindings *_downscaleYuv420Srb = nullptr;
	QRhiShaderResourceBindings *_blurHSrb = nullptr;

	QRhiRenderPassDescriptor *_offscreenRpDesc = nullptr;

	QRhiTexture *_noiseTexture = nullptr;

	Ui::Rhi::Image _buttons;
	QRect _pinOn;
	QRect _pinOff;
	QRect _back;
	QRect _muteOn;
	QRect _muteOff;
	QRect _pausedIcon;

	Ui::Rhi::Image _names;
	QRect _pausedTextRect;
	std::vector<TileData> _tileData;
	std::vector<int> _tileDataIndices;

	std::vector<PoolTexture> _texturePool;
	int _nextPoolIndex = 0;

	std::vector<QRhiShaderResourceBindings*> _perDrawSrbs;

	Ui::CrossLineAnimation _pinIcon;
	Ui::CrossLineAnimation _muteIcon;

	Ui::RoundRect _pinBackground;

	bool _initialized = false;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group

#endif // Qt >= 6.7
