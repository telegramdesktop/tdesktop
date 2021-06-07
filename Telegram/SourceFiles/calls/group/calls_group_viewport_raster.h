/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/round_rect.h"
#include "ui/effects/cross_line.h"
#include "ui/gl/gl_surface.h"
#include "ui/text/text.h"

namespace Calls::Group {

class Viewport::RendererSW final : public Ui::GL::Renderer {
public:
	explicit RendererSW(not_null<Viewport*> owner);

	void paintFallback(
		Painter &&p,
		const QRegion &clip,
		Ui::GL::Backend backend) override;

private:
	struct TileData {
		QImage userpicFrame;
		QImage blurredFrame;
		bool stale = false;
	};
	void paintTile(
		Painter &p,
		not_null<VideoTile*> tile,
		const QRect &clip,
		QRegion &bg);
	void paintTileOutline(
		Painter &p,
		int x,
		int y,
		int width,
		int height,
		not_null<VideoTile*> tile);
	void paintTileControls(
		Painter &p,
		int x,
		int y,
		int width,
		int height,
		not_null<VideoTile*> tile);
	void validateUserpicFrame(
		not_null<VideoTile*> tile,
		TileData &data);

	const not_null<Viewport*> _owner;

	QImage _shadow;
	bool _userpicFrame = false;
	bool _pausedFrame = false;
	base::flat_map<not_null<VideoTile*>, TileData> _tileData;
	Ui::CrossLineAnimation _pinIcon;
	Ui::RoundRect _pinBackground;

};

} // namespace Calls::Group
