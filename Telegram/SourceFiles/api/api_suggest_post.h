/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ClickHandler;

namespace Main {
class SessionShow;
} // namespace Main

namespace Api {

[[nodiscard]] std::shared_ptr<ClickHandler> AcceptClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] std::shared_ptr<ClickHandler> DeclineClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] std::shared_ptr<ClickHandler> SuggestChangesClickHandler(
	not_null<HistoryItem*> item);

void AddOfferToMessage(
	std::shared_ptr<Main::SessionShow> show,
	FullMsgId itemId);

} // namespace Api
