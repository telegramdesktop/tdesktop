/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/view/media_view_pip_renderer.h"

namespace Media::View {

class Pip::RendererSW final : public Pip::Renderer {
public:
	explicit RendererSW(not_null<Pip*> owner);

	void paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) override;

private:
	void paintTransformedVideoFrame(ContentGeometry geometry) override;
	void paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry) override;
	void paintTransformedImage(
		const QImage &image,
		ContentGeometry geometry);
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

	void paintFade(ContentGeometry geometry) const;

	[[nodiscard]] FrameRequest frameRequest(ContentGeometry geometry) const;
	[[nodiscard]] QImage staticContentByRequest(
		const QImage &image,
		const FrameRequest &request);

	const not_null<Pip*> _owner;

	Painter *_p = nullptr;
	const QRegion *_clip = nullptr;
	QRect _clipOuter;

	Ui::RoundRect _roundRect;

	QImage _preparedStaticContent;
	FrameRequest _preparedStaticRequest;
	qint64 _preparedStaticKey = 0;

};

} // namespace Media::View
