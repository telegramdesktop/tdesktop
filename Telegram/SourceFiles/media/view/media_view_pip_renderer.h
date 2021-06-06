/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_pip.h"
#include "ui/gl/gl_surface.h"

namespace Media::View {

class Pip::Renderer : public Ui::GL::Renderer {
public:
	virtual void paintTransformedVideoFrame(ContentGeometry geometry) = 0;
	virtual void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) = 0;
	virtual void paintRadialLoading(
		QRect inner,
		float64 controlsShown) = 0;
	virtual void paintButtonsStart() = 0;
	virtual void paintButton(
		const Button &button,
		int outerWidth,
		float64 shown,
		float64 over,
		const style::icon &icon,
		const style::icon &iconOver) = 0;
	virtual void paintPlayback(QRect outer, float64 shown) = 0;
	virtual void paintVolumeController(QRect outer, float64 shown) = 0;

};

} // namespace Media::View
