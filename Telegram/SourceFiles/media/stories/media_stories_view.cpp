/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_view.h"

#include "media/stories/media_stories_controller.h"
#include "media/stories/media_stories_delegate.h"
#include "media/stories/media_stories_header.h"
#include "media/stories/media_stories_slider.h"
#include "media/stories/media_stories_reply.h"

namespace Media::Stories {

View::View(not_null<Delegate*> delegate)
: _controller(std::make_unique<Controller>(delegate)) {
}

View::~View() = default;

void View::show(const Data::StoriesList &list, int index) {
	_controller->show(list, index);
}

void View::ready() {
	_controller->ready();
}

QRect View::contentGeometry() const {
	return _controller->layout().content;
}

void View::updatePlayback(const Player::TrackState &state) {
	_controller->updateVideoPlayback(state);
}

bool View::jumpAvailable(int delta) const {
	return _controller->jumpAvailable(delta);
}

bool View::jumpFor(int delta) const {
	return _controller->jumpFor(delta);
}

bool View::paused() const {
	return _controller->paused();
}

void View::togglePaused(bool paused) {
	_controller->togglePaused(paused);
}

} // namespace Media::Stories
