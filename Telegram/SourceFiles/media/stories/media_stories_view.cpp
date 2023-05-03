/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_view.h"

#include "media/stories/media_stories_delegate.h"
#include "media/stories/media_stories_header.h"
#include "media/stories/media_stories_slider.h"
#include "media/stories/media_stories_reply.h"

namespace Media::Stories {

View::View(not_null<Delegate*> delegate)
: _delegate(delegate)
, _wrap(_delegate->storiesWrap())
, _header(std::make_unique<Header>(_delegate))
, _slider(std::make_unique<Slider>())
, _replyArea(std::make_unique<ReplyArea>(_delegate)) {
}

View::~View() = default;

void View::show(const Data::StoriesList &list, int index) {
	Expects(index < list.items.size());

	const auto &item = list.items[index];
	_header->show({
		.user = list.user,
		.date = item.date,
	});
}

} // namespace Media::Stories
