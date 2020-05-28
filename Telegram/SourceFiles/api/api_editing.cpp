/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_editing.h"

#include "apiwrap.h"
#include "api/api_media.h"
#include "api/api_text_entities.h"
#include "boxes/confirm_box.h"
#include "data/data_scheduled_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Api {
namespace {

void EditMessage(
		not_null<HistoryItem*> item,
		std::optional<MTPInputMedia> inputMedia,
		SendOptions options,
		Fn<void(const MTPUpdates &, Fn<void()>)> done,
		Fn<void(const RPCError &)> fail) {
	const auto session = &item->history()->session();
	const auto api = &session->api();

	const auto text = item->originalText().text;
	const auto sentEntities = EntitiesToMTP(
		session,
		item->originalText().entities,
		ConvertOption::SkipLocal);
	const auto media = item->media();

	const auto emptyFlag = MTPmessages_EditMessage::Flag(0);
	const auto flags = emptyFlag
	| (!text.isEmpty()
		? MTPmessages_EditMessage::Flag::f_message
		: emptyFlag)
	| ((media && inputMedia.has_value())
		? MTPmessages_EditMessage::Flag::f_media
		: emptyFlag)
	| ((!media || !media->webpage())
		? MTPmessages_EditMessage::Flag::f_no_webpage
		: emptyFlag)
	| (!sentEntities.v.isEmpty()
		? MTPmessages_EditMessage::Flag::f_entities
		: emptyFlag)
	| (options.scheduled
		? MTPmessages_EditMessage::Flag::f_schedule_date
		: emptyFlag);

	const auto id = item->isScheduled()
		? session->data().scheduledMessages().lookupId(item)
		: item->id;
	api->request(MTPmessages_EditMessage(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(id),
		MTP_string(text),
		inputMedia.value_or(MTPInputMedia()),
		MTPReplyMarkup(),
		sentEntities,
		MTP_int(options.scheduled)
	)).done([=](const MTPUpdates &result) {
		done(result, [=] { api->applyUpdates(result); });
	}).fail(
		fail
	).send();
}

} // namespace

void RescheduleMessage(
		not_null<HistoryItem*> item,
		SendOptions options) {
	const auto done = [=](const auto &result, Fn<void()> applyUpdates) {
		applyUpdates();
	};
	const auto fail = [](const RPCError &error) {};

	EditMessage(item, std::nullopt, options, done, fail);
}


} // namespace Api
