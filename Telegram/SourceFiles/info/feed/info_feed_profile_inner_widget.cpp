/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_profile_inner_widget.h"

#include "info/info_controller.h"
#include "info/feed/info_feed_profile_widget.h"
#include "info/feed/info_feed_cover.h"
#include "info/feed/info_feed_channels.h"
#include "info/profile/info_profile_actions.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"

namespace Info {
namespace FeedProfile {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _feed(_controller->key().feed())
, _content(setupContent(this)) {
	_content->heightValue(
	) | rpl::start_with_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	_cover = result->add(object_ptr<Cover>(
		result,
		_controller));
	auto details = Profile::SetupFeedDetails(_controller, parent, _feed);
	result->add(std::move(details));
	result->add(object_ptr<BoxContentDivider>(result));

	_channels = result->add(object_ptr<Channels>(
		result,
		_controller)
	);
	_channels->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto min = (request.ymin < 0)
			? request.ymin
			: mapFromGlobal(_channels->mapToGlobal({ 0, request.ymin })).y();
		auto max = (request.ymin < 0)
			? mapFromGlobal(_channels->mapToGlobal({ 0, 0 })).y()
			: (request.ymax < 0)
			? request.ymax
			: mapFromGlobal(_channels->mapToGlobal({ 0, request.ymax })).y();
		_scrollToRequests.fire({ min, max });
	}, _channels->lifetime());

	return std::move(result);
}

int InnerWidget::countDesiredHeight() const {
	return _content->height() + (_channels
		? (_channels->desiredHeight() - _channels->height())
		: 0);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	if (_channels) {
		memento->setChannelsState(_channels->saveState());
	}
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (_channels) {
		_channels->restoreState(memento->channelsState());
	}
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<int> InnerWidget::desiredHeightValue() const {
	return _desiredHeight.events_starting_with(countDesiredHeight());
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	updateDesiredHeight();
	return _content->heightNoMargins();
}

} // namespace FeedProfile
} // namespace Info
