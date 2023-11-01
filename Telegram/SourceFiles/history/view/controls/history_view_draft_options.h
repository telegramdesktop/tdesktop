/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_drafts.h"

class History;
struct MessageLinkRange;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Controls {

class WebpageResolver;

struct EditDraftOptionsArgs {
	std::shared_ptr<ChatHelpers::Show> show;
	not_null<History*> history;
	Data::Draft draft;
	QString usedLink;
	std::vector<MessageLinkRange> links;
	std::shared_ptr<WebpageResolver> resolver;
	Fn<void(FullReplyTo, Data::WebPageDraft)> done;
	Fn<void(FullReplyTo)> highlight;
	Fn<void()> clearOldDraft;
};

void EditDraftOptions(EditDraftOptionsArgs &&args);

void ShowReplyToChatBox(
	std::shared_ptr<ChatHelpers::Show> show,
	FullReplyTo reply,
	Fn<void()> clearOldDraft = nullptr);

} // namespace HistoryView::Controls
