/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/item_text_options.h"

#include "history/history.h"
#include "history/history_item.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"

namespace Ui {
namespace {

bool UseBotTextOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	if (const auto user = history->peer->asUser()) {
		if (user->isBot()) {
			return true;
		}
	} else if (const auto chat = history->peer->asChat()) {
		if (chat->botStatus >= 0) {
			return true;
		}
	} else if (const auto group = history->peer->asMegagroup()) {
		if (group->mgInfo->botStatus >= 0) {
			return true;
		}
	}
	if (const auto user = author->asUser()) {
		if (user->isBot()) {
			return true;
		}
	}
	return false;
}

} // namespace

const TextParseOptions &ItemTextOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	return UseBotTextOptions(history, author)
		? ItemTextBotDefaultOptions()
		: ItemTextDefaultOptions();
}

const TextParseOptions &ItemTextOptions(not_null<const HistoryItem*> item) {
	return ItemTextOptions(item->history(), item->author());
}

const TextParseOptions &ItemTextNoMonoOptions(
		not_null<History*> history,
		not_null<PeerData*> author) {
	return UseBotTextOptions(history, author)
		? ItemTextBotNoMonoOptions()
		: ItemTextNoMonoOptions();
}

const TextParseOptions &ItemTextNoMonoOptions(
		not_null<const HistoryItem*> item) {
	return ItemTextNoMonoOptions(item->history(), item->author());
}

} // namespace Ui
