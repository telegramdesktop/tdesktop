/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef OS_OSX

#include "media/view/media_view_playback_controls.h"
#include "media/view/media_view_overlay_widget.h"

namespace TouchBar {

void SetupMediaViewTouchBar(
	WId winId,
	not_null<Media::View::PlaybackControls::Delegate*> controlsDelegate,
	rpl::producer<Media::Player::TrackState> trackState,
	rpl::producer<Media::View::OverlayWidget::TouchBarItemType> display,
	rpl::producer<bool> fullscreenToggled);

} // namespace TouchBar

#endif // OS_OSX
