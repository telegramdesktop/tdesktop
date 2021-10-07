/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_edition.h"

#include "api/api_text_entities.h"
#include "main/main_session.h"

HistoryMessageEdition::HistoryMessageEdition(
		not_null<Main::Session*> session,
		const MTPDmessage &message) {
	isEditHide = message.is_edit_hide();
	editDate = message.vedit_date().value_or(-1);
	textWithEntities = TextWithEntities{
		qs(message.vmessage()),
		Api::EntitiesFromMTP(
			session,
			message.ventities().value_or_empty())
	};
	replyMarkup = HistoryMessageMarkupData(message.vreply_markup());
	mtpMedia = message.vmedia();
	views = message.vviews().value_or(-1);
	forwards = message.vforwards().value_or(-1);
	if (const auto mtpReplies = message.vreplies()) {
		replies = HistoryMessageRepliesData(mtpReplies);
	}

	const auto period = message.vttl_period();
	ttl = (period && period->v > 0) ? (message.vdate().v + period->v) : 0;
}
