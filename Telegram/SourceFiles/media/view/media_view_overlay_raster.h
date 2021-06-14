/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_renderer.h"

namespace Media::View {

class OverlayWidget::RendererSW final : public OverlayWidget::Renderer {
public:
	explicit RendererSW(not_null<OverlayWidget*> owner);

	void paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) override;

private:
	void paintBackground() override;
	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground) override;
	void paintTransformedImage(
		const QImage &image,
		QRect rect,
		int rotation);
	void paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) override;
	void paintThemePreview(QRect outer) override;
	void paintDocumentBubble(QRect outer, QRect icon) override;
	void paintSaveMsg(QRect outer) override;
	void paintControlsStart() override;
	void paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) override;
	void paintFooter(QRect outer, float64 opacity) override;
	void paintCaption(QRect outer, float64 opacity) override;
	void paintGroupThumbs(QRect outer, float64 opacity) override;

	[[nodiscard]] static QRect TransformRect(QRectF geometry, int rotation);

	const not_null<OverlayWidget*> _owner;
	QBrush _transparentBrush;

	Painter *_p = nullptr;
	const QRegion *_clip = nullptr;
	QRect _clipOuter;

};

} // namespace Media::View
