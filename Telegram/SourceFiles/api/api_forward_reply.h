/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "data/data_drafts.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Api {

struct ForwardReply {
	std::shared_ptr<ChatHelpers::Show> show;
	SendAction action;
	TextWithTags text;
	Data::WebPageDraft webPage;
	Fn<bool(int messagesCount)> checkPayment;
	Fn<void(FullMsgId)> forwarded;
};

[[nodiscard]] bool CanReplyToForward(const HistoryItemsList &items);
bool SendForwardWithReply(ForwardReply &&descriptor);

} // namespace Api
