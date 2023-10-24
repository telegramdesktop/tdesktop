/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Controls {

void EditReplyOptions(
	std::shared_ptr<ChatHelpers::Show> show,
	FullReplyTo reply,
	Fn<void(FullReplyTo)> done,
	Fn<void()> highlight,
	Fn<void()> clearOldDraft);

void ShowReplyToChatBox(
	std::shared_ptr<ChatHelpers::Show> show,
	FullReplyTo reply,
	Fn<void()> clearOldDraft = nullptr);

} // namespace HistoryView::Controls
