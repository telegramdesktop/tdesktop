/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_widget.h"

namespace Media::View {

void OverlayWidget::restartFailedStreamingAt(crl::time position) {
	const auto wasFullScreen = _fullScreenVideo;
	_streamingStartPaused = false;
	clearStreaming(false);
	if (!canInitStreaming() || !prepareStreaming()) {
		redisplayContent();
	} else {
		restartAtSeekPosition(position);
		restoreReplacedStreaming(wasFullScreen);
	}
}

} // namespace Media::View
