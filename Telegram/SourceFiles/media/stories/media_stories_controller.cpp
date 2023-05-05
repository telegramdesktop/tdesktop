/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_controller.h"

#include "data/data_stories.h"
#include "media/stories/media_stories_delegate.h"
#include "media/stories/media_stories_header.h"
#include "media/stories/media_stories_slider.h"
#include "media/stories/media_stories_reply.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h" // UserpicButton

namespace Media::Stories {

Controller::Controller(not_null<Delegate*> delegate)
: _delegate(delegate)
, _wrap(_delegate->storiesWrap())
, _header(std::make_unique<Header>(this))
, _slider(std::make_unique<Slider>(this))
, _replyArea(std::make_unique<ReplyArea>(this)) {
	initLayout();
}

void Controller::initLayout() {
	const auto headerHeight = st::storiesHeaderMargin.top()
		+ st::storiesHeaderPhoto.photoSize
		+ st::storiesHeaderMargin.bottom();
	const auto sliderHeight = st::storiesSliderMargin.top()
		+ st::storiesSlider.width
		+ st::storiesSliderMargin.bottom();
	const auto outsideHeaderHeight = headerHeight + sliderHeight;
	const auto fieldMinHeight = st::storiesFieldMargin.top()
		+ st::storiesAttach.height
		+ st::storiesFieldMargin.bottom();
	const auto minHeightForOutsideHeader = st::storiesMaxSize.height()
		+ outsideHeaderHeight
		+ fieldMinHeight;
	_layout = _wrap->sizeValue(
	) | rpl::map([=](QSize size) {
		size = QSize(
			std::max(size.width(), st::mediaviewMinWidth),
			std::max(size.height(), st::mediaviewMinHeight));

		auto layout = Layout();
		layout.headerLayout = (size.height() >= minHeightForOutsideHeader)
			? HeaderLayout::Outside
			: HeaderLayout::Normal;

		const auto topSkip = (layout.headerLayout == HeaderLayout::Outside)
			? outsideHeaderHeight
			: st::storiesFieldMargin.bottom();
		const auto bottomSkip = fieldMinHeight;
		const auto maxWidth = size.width() - 2 * st::storiesSideSkip;
		const auto availableHeight = size.height() - topSkip - bottomSkip;
		const auto maxContentHeight = std::min(
			availableHeight,
			st::storiesMaxSize.height());
		const auto nowWidth = maxContentHeight * st::storiesMaxSize.width()
			/ st::storiesMaxSize.height();
		const auto contentWidth = std::min(nowWidth, maxWidth);
		const auto contentHeight = (contentWidth < nowWidth)
			? (contentWidth * st::storiesMaxSize.height()
				/ st::storiesMaxSize.width())
			: maxContentHeight;
		const auto addedTopSkip = (availableHeight - contentHeight) / 2;
		layout.content = QRect(
			(size.width() - contentWidth) / 2,
			addedTopSkip + topSkip,
			contentWidth,
			contentHeight);

		if (layout.headerLayout == HeaderLayout::Outside) {
			layout.header = QRect(
				layout.content.topLeft() - QPoint(0, outsideHeaderHeight),
				QSize(contentWidth, outsideHeaderHeight));
			layout.slider = QRect(
				layout.header.topLeft() + QPoint(0, headerHeight),
				QSize(contentWidth, sliderHeight));
		} else {
			layout.slider = QRect(
				layout.content.topLeft(),
				QSize(contentWidth, sliderHeight));
			layout.header = QRect(
				layout.slider.topLeft() + QPoint(0, sliderHeight),
				QSize(contentWidth, headerHeight));
		}
		layout.controlsWidth = std::max(
			layout.content.width(),
			st::storiesControlsMinWidth);
		layout.controlsBottomPosition = QPoint(
			(size.width() - layout.controlsWidth) / 2,
			(layout.content.y()
				+ layout.content.height()
				+ fieldMinHeight
				- st::storiesFieldMargin.bottom()));
		layout.autocompleteRect = QRect(
			layout.controlsBottomPosition.x(),
			0,
			layout.controlsWidth,
			layout.controlsBottomPosition.y());

		return layout;
	});
}

not_null<Ui::RpWidget*> Controller::wrap() const {
	return _wrap;
}

Layout Controller::layout() const {
	Expects(_layout.current().has_value());

	return *_layout.current();
}

rpl::producer<Layout> Controller::layoutValue() const {
	return _layout.value() | rpl::filter_optional();
}

std::shared_ptr<ChatHelpers::Show> Controller::uiShow() const {
	return _delegate->storiesShow();
}

auto Controller::stickerOrEmojiChosen() const
->rpl::producer<ChatHelpers::FileChosen> {
	return _delegate->storiesStickerOrEmojiChosen();
}

void Controller::show(const Data::StoriesList &list, int index) {
	Expects(index < list.items.size());

	const auto &item = list.items[index];

	const auto id = ShownId{
		.user = list.user,
		.id = item.id,
	};
	if (_shown == id) {
		return;
	}
	_shown = id;

	_header->show({ .user = list.user, .date = item.date });
	_slider->show({ .index = index, .total = int(list.items.size()) });
	_replyArea->show({ .user = list.user });
}

} // namespace Media::Stories
