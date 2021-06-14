/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_overlay_widget.h"

namespace Media::View {

class OverlayWidget::Renderer : public Ui::GL::Renderer {
public:
	virtual void paintBackground() = 0;
	virtual void paintTransformedVideoFrame(ContentGeometry geometry) = 0;
	virtual void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground) = 0;
	virtual void paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) = 0;
	virtual void paintThemePreview(QRect outer) = 0;
	virtual void paintDocumentBubble(QRect outer, QRect icon) = 0;
	virtual void paintSaveMsg(QRect outer) = 0;
	virtual void paintControlsStart() = 0;
	virtual void paintControl(
		OverState control,
		QRect outer,
		float64 outerOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) = 0;
	virtual void paintFooter(QRect outer, float64 opacity) = 0;
	virtual void paintCaption(QRect outer, float64 opacity) = 0;
	virtual void paintGroupThumbs(QRect outer, float64 opacity) = 0;

};

} // namespace Media::View
