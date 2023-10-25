/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_drafts.h"

class History;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Controls {

void EditDraftOptions(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<History*> history,
	Data::Draft draft,
	Fn<void(FullReplyTo, Data::WebPageDraft)> done,
	Fn<void()> highlight,
	Fn<void()> clearOldDraft);

void ShowReplyToChatBox(
	std::shared_ptr<ChatHelpers::Show> show,
	FullReplyTo reply,
	Fn<void()> clearOldDraft = nullptr);

} // namespace HistoryView::Controls
