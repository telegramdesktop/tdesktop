/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_view.h"

#include "data/data_file_origin.h"
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

void View::show(
		not_null<Data::Story*> story,
		Data::StoriesContext context) {
	_controller->show(story, context);
}

void View::ready() {
	_controller->ready();
}

Data::Story *View::story() const {
	return _controller->story();
}

QRect View::finalShownGeometry() const {
	return _controller->layout().content;
}

rpl::producer<QRect> View::finalShownGeometryValue() const {
	return _controller->layoutValue(
		) | rpl::map([=](const Layout &layout) {
			return layout.content;
		}) | rpl::distinct_until_changed();
}

ContentLayout View::contentLayout() const {
	return _controller->contentLayout();
}

bool View::closeByClickAt(QPoint position) const {
	return _controller->closeByClickAt(position);
}

void View::updatePlayback(const Player::TrackState &state) {
	_controller->updateVideoPlayback(state);
}

ClickHandlerPtr View::lookupAreaHandler(QPoint point) const {
	return _controller->lookupAreaHandler(point);
}

bool View::subjumpAvailable(int delta) const {
	return _controller->subjumpAvailable(delta);
}

bool View::subjumpFor(int delta) const {
	return _controller->subjumpFor(delta);
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

void View::contentPressed(bool pressed) {
	_controller->contentPressed(pressed);
}

void View::menuShown(bool shown) {
	_controller->setMenuShown(shown);
}

void View::shareRequested() {
	_controller->shareRequested();
}

void View::deleteRequested() {
	_controller->deleteRequested();
}

void View::reportRequested() {
	_controller->reportRequested();
}

void View::togglePinnedRequested(bool pinned) {
	_controller->togglePinnedRequested(pinned);
}

bool View::ignoreWindowMove(QPoint position) const {
	return _controller->ignoreWindowMove(position);
}

void View::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	_controller->tryProcessKeyInput(e);
}

bool View::allowStealthMode() const {
	return _controller->allowStealthMode();
}

void View::setupStealthMode() {
	_controller->setupStealthMode();
}

auto View::attachReactionsToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition)
-> AttachStripResult {
	return _controller->attachReactionsToMenu(menu, desiredPosition);
}

SiblingView View::sibling(SiblingType type) const {
	return _controller->sibling(type);
}

Data::FileOrigin View::fileOrigin() const {
	return _controller->fileOrigin();
}

TextWithEntities View::captionText() const {
	return _controller->captionText();
}

bool View::skipCaption() const {
	return _controller->skipCaption();
}

void View::showFullCaption() {
	_controller->showFullCaption();
}

rpl::lifetime &View::lifetime() {
	return _controller->lifetime();
}

} // namespace Media::Stories
