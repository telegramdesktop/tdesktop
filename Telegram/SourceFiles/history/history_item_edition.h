/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "history/history_item_reply_markup.h"

namespace Main {
class Session;
} // namespace Main

struct HistoryMessageEdition {
	explicit HistoryMessageEdition() = default;
	HistoryMessageEdition(
		not_null<Main::Session*> session,
		const MTPDmessage &message);

	bool isEditHide = false;
	int editDate = 0;
	int views = -1;
	int forwards = -1;
	int ttl = 0;
	bool useSameViews = false;
	bool useSameForwards = false;
	bool useSameReplies = false;
	bool useSameMarkup = false;
	bool useSameReactions = false;
	bool savePreviousMedia = false;
	TextWithEntities textWithEntities;
	HistoryMessageMarkupData replyMarkup;
	HistoryMessageRepliesData replies;
	const MTPMessageMedia *mtpMedia = nullptr;
	const MTPMessageReactions *mtpReactions = nullptr;
};
