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
#include "mtproto/mtproto_rpc_sender.h"

namespace Api {
namespace {

mtpRequestId EditMessage(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		SendOptions options,
		Fn<void(const MTPUpdates &, Fn<void()>)> done,
		Fn<void(const RPCError &)> fail,
		std::optional<MTPInputMedia> inputMedia = std::nullopt) {
	const auto session = &item->history()->session();
	const auto api = &session->api();

	const auto text = textWithEntities.text;
	const auto sentEntities = EntitiesToMTP(
		session,
		textWithEntities.entities,
		ConvertOption::SkipLocal);
	const auto media = item->media();

	const auto emptyFlag = MTPmessages_EditMessage::Flag(0);
	const auto flags = emptyFlag
	| (!text.isEmpty() || media
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
	return api->request(MTPmessages_EditMessage(
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

mtpRequestId EditMessage(
		not_null<HistoryItem*> item,
		SendOptions options,
		Fn<void(const MTPUpdates &, Fn<void()>)> done,
		Fn<void(const RPCError &)> fail,
		std::optional<MTPInputMedia> inputMedia = std::nullopt) {
	const auto &text = item->originalText();
	return EditMessage(item, text, options, done, fail, inputMedia);
}

void EditMessageWithUploadedMedia(
		not_null<HistoryItem*> item,
		SendOptions options,
		MTPInputMedia media) {
	const auto done = [=](const auto &result, Fn<void()> applyUpdates) {
		if (item) {
			item->clearSavedMedia();
			item->setIsLocalUpdateMedia(true);
			applyUpdates();
			item->setIsLocalUpdateMedia(false);
		}
	};
	const auto fail = [=](const RPCError &error) {
		const auto err = error.type();
		const auto session = &item->history()->session();
		const auto notModified = (err == u"MESSAGE_NOT_MODIFIED"_q);
		const auto mediaInvalid = (err == u"MEDIA_NEW_INVALID"_q);
		if (notModified || mediaInvalid) {
			item->returnSavedMedia();
			session->data().sendHistoryChangeNotifications();
			if (mediaInvalid) {
				Ui::show(
					Box<InformBox>(tr::lng_edit_media_invalid_file(tr::now)),
					Ui::LayerOption::KeepOther);
			}
		} else {
			session->api().sendMessageFail(error, item->history()->peer);
		}
	};

	EditMessage(item, options, done, fail, media);
}

} // namespace

void RescheduleMessage(
		not_null<HistoryItem*> item,
		SendOptions options) {
	const auto done = [=](const auto &result, Fn<void()> applyUpdates) {
		applyUpdates();
	};
	const auto fail = [](const RPCError &error) {};

	EditMessage(item, options, done, fail);
}

void EditMessageWithUploadedDocument(
		HistoryItem *item,
		const MTPInputFile &file,
		const std::optional<MTPInputFile> &thumb,
		SendOptions options) {
	if (!item || !item->media() || !item->media()->document()) {
		return;
	}
	const auto media = PrepareUploadedDocument(item, file, thumb);
	EditMessageWithUploadedMedia(item, options, media);
}

void EditMessageWithUploadedPhoto(
		HistoryItem *item,
		const MTPInputFile &file,
		SendOptions options) {
	if (!item || !item->media() || !item->media()->photo()) {
		return;
	}
	const auto media = PrepareUploadedPhoto(file);
	EditMessageWithUploadedMedia(item, options, media);
}

mtpRequestId EditCaption(
		not_null<HistoryItem*> item,
		const TextWithEntities &caption,
		Fn<void(const MTPUpdates &)> done,
		Fn<void(const RPCError &)> fail) {
	const auto callback = [=](const auto &result, Fn<void()> applyUpdates) {
		done(result);
		applyUpdates();
	};
	return EditMessage(item, caption, SendOptions(), callback, fail);
}


} // namespace Api
