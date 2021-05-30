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
	[[nodiscard]] bool pinned() const {
		return _pinned;
	}
	[[nodiscard]] QRect pinOuter() const;
	[[nodiscard]] QRect pinInner() const;
	[[nodiscard]] const VideoEndpoint &endpoint() const {
		return _endpoint;
	}
	[[nodiscard]] QSize trackSize() const {
		return _trackSize.current();
	}
	[[nodiscard]] rpl::producer<QSize> trackSizeValue() const {
		return _trackSize.value();
	}

	[[nodiscard]] bool screencast() const;
	void setGeometry(QRect geometry);
	void togglePinShown(bool shown);
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

private:
	void setup(rpl::producer<bool> pinned);
	[[nodiscard]] int pinSlide() const;
	void updatePinnedGeometry();

	const VideoEndpoint _endpoint;
	const Fn<void()> _update;

	VideoTileTrack _track;
	QRect _geometry;
	rpl::variable<QSize> _trackSize;
	QRect _pinOuter;
	QRect _pinInner;
	Ui::Animations::Simple _pinShownAnimation;
	bool _pinShown = false;
	bool _pinned = false;
	std::optional<VideoQuality> _quality;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
