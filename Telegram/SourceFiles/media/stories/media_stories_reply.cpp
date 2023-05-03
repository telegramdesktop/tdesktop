/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_reply.h"

#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/tabbed_selector.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "media/stories/media_stories_delegate.h"
#include "menu/menu_send.h"

namespace Media::Stories {

ReplyArea::ReplyArea(not_null<Delegate*> delegate)
: _delegate(delegate)
, _controls(std::make_unique<HistoryView::ComposeControls>(
	_delegate->storiesWrap(),
	HistoryView::ComposeControlsDescriptor{
		.show = _delegate->storiesShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			showPremiumToast(emoji);
		},
		.mode = HistoryView::ComposeControlsMode::Normal,
		.sendMenuType = SendMenu::Type::SilentOnly,
		.stickerOrEmojiChosen = _delegate->storiesStickerOrEmojiChosen(),
	}
)) {
	_delegate->storiesWrap()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_controls->resizeToWidth(size.width() - 200);
		_controls->move(100, size.height() - _controls->heightCurrent() - 20);
		_controls->setAutocompleteBoundingRect({ QPoint() ,size });
	}, _lifetime);

	_controls->show();
	_controls->showFinished();
}

ReplyArea::~ReplyArea() {
}

void ReplyArea::showPremiumToast(not_null<DocumentData*> emoji) {
	// #TODO stories
}

} // namespace Media::Stories
