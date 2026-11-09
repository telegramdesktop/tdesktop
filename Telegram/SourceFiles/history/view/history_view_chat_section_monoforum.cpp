/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_chat_section.h"

#include "data/data_channel.h"
#include "history/view/controls/history_view_compose_controls.h"

namespace HistoryView {

bool ChatWidget::monoforumReplyMissing() const {
	const auto channel = _peer->asChannel();
	if (mode() != Mode::History
		|| !channel
		|| !channel->amMonoforumAdmin()
		|| channel->monoforumDisabled()) {
		return false;
	}
	const auto reply = _composeControls->replyingToMessage();
	return !reply.messageId || (reply.messageId.peer != _peer->id);
}

bool ChatWidget::monoforumReplyRequired() const {
	return !_composeControls->isEditingMessage() && monoforumReplyMissing();
}

} // namespace HistoryView
