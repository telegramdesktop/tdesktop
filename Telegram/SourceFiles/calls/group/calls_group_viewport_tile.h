/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "calls/group/calls_group_call.h"
#include "ui/effects/animations.h"

class Painter;
class QOpenGLFunctions;

namespace Ui {
class CrossLineAnimation;
class RoundRect;
} // namespace Ui

namespace Calls::Group {

class Viewport::VideoTile final {
public:
	VideoTile(
		const VideoEndpoint &endpoint,
		VideoTileTrack track,
		rpl::producer<QSize> trackSize,
		rpl::producer<bool> pinned,
		Fn<void()> update);

	[[nodiscard]] not_null<Webrtc::VideoTrack*> track() const {
		return _track.track;
	}
	[[nodiscard]] not_null<MembersRow*> row() const {
		return _track.row;
	}
	[[nodiscard]] QRect geometry() const {
		return _geometry;
	}
	[[nodiscard]] TileAnimation animation() const {
		return _animation;
	}
	[[nodiscard]] bool pinned() const {
		return _pinned;
	}
	[[nodiscard]] bool hidden() const {
		return _hidden;
	}
	[[nodiscard]] bool visible() const {
		return !_hidden && !_geometry.isEmpty();
	}
	[[nodiscard]] QRect pinOuter() const;
	[[nodiscard]] QRect pinInner() const;
	[[nodiscard]] QRect backOuter() const;
	[[nodiscard]] QRect backInner() const;
	[[nodiscard]] const VideoEndpoint &endpoint() const {
		return _endpoint;
	}
	[[nodiscard]] QSize trackSize() const {
		return _trackSize.current();
	}
	[[nodiscard]] rpl::producer<QSize> trackSizeValue() const {
		return _trackSize.value();
	}
	[[nodiscard]] QSize trackOrUserpicSize() const;
	[[nodiscard]] static QSize PausedVideoSize();

	[[nodiscard]] bool screencast() const;
	void setGeometry(
		QRect geometry,
		TileAnimation animation = TileAnimation());
	void hide();
	void toggleTopControlsShown(bool shown);
	bool updateRequestedQuality(VideoQuality quality);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	[[nodiscard]] static QSize PinInnerSize(bool pinned);
	static void PaintPinButton(
		Painter &p,
		bool pinned,
		int x,
		int y,
		int outerWidth,
		not_null<Ui::RoundRect*> background,
		not_null<Ui::CrossLineAnimation*> icon);

	[[nodiscard]] static QSize BackInnerSize();
	static void PaintBackButton(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		not_null<Ui::RoundRect*> background);

private:
	void setup(rpl::producer<bool> pinned);
	[[nodiscard]] int topControlsSlide() const;
	void updateTopControlsSize();
	void updateTopControlsPosition();

	const VideoEndpoint _endpoint;
	const Fn<void()> _update;

	VideoTileTrack _track;
	QRect _geometry;
	TileAnimation _animation;
	rpl::variable<QSize> _trackSize;
	mutable QSize _userpicSize;
	QRect _pinOuter;
	QRect _pinInner;
	QRect _backOuter;
	QRect _backInner;
	Ui::Animations::Simple _topControlsShownAnimation;
	bool _topControlsShown = false;
	bool _pinned = false;
	bool _hidden = true;
	std::optional<VideoQuality> _quality;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
