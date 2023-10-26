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
#include "ui/boxes/confirm_box.h"
#include "data/data_histories.h"
#include "data/data_scheduled_messages.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "boxes/abstract_box.h" // Ui::show().

namespace Api {
namespace {

using namespace rpl::details;

template <typename T>
constexpr auto WithId =
	is_callable_plain_v<T, Fn<void()>, mtpRequestId>;
template <typename T>
constexpr auto WithoutId =
	is_callable_plain_v<T, Fn<void()>>;
template <typename T>
constexpr auto WithoutCallback =
	is_callable_plain_v<T>;
template <typename T>
constexpr auto ErrorWithId =
	is_callable_plain_v<T, QString, mtpRequestId>;
template <typename T>
constexpr auto ErrorWithoutId =
	is_callable_plain_v<T, QString>;

template <typename DoneCallback, typename FailCallback>
mtpRequestId EditMessage(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		Data::WebPageDraft webpage,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail,
		std::optional<MTPInputMedia> inputMedia = std::nullopt) {
	const auto session = &item->history()->session();
	const auto api = &session->api();

	const auto text = textWithEntities.text;
	const auto sentEntities = EntitiesToMTP(
		session,
		textWithEntities.entities,
		ConvertOption::SkipLocal);
	const auto media = item->media();

	const auto updateRecentStickers = inputMedia.has_value()
		? Api::HasAttachedStickers(*inputMedia)
		: false;

	const auto emptyFlag = MTPmessages_EditMessage::Flag(0);
	const auto flags = emptyFlag
	| ((!text.isEmpty() || media)
		? MTPmessages_EditMessage::Flag::f_message
		: emptyFlag)
	| ((media && inputMedia.has_value())
		? MTPmessages_EditMessage::Flag::f_media
		: emptyFlag)
	| (webpage.removed
		? MTPmessages_EditMessage::Flag::f_no_webpage
		: emptyFlag)
	| ((!webpage.removed && !webpage.url.isEmpty())
		? MTPmessages_EditMessage::Flag::f_media
		: emptyFlag)
	| ((!webpage.removed && !webpage.url.isEmpty() && webpage.invert)
		? MTPmessages_EditMessage::Flag::f_invert_media
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
		inputMedia.value_or(Data::WebPageForMTP(webpage, text.isEmpty())),
		MTPReplyMarkup(),
		sentEntities,
		MTP_int(options.scheduled)
	)).done([=](
			const MTPUpdates &result,
			[[maybe_unused]] mtpRequestId requestId) {
		const auto apply = [=] { api->applyUpdates(result); };

		if constexpr (WithId<DoneCallback>) {
			done(apply, requestId);
		} else if constexpr (WithoutId<DoneCallback>) {
			done(apply);
		} else if constexpr (WithoutCallback<DoneCallback>) {
			done();
			apply();
		} else {
			t_bad_callback(done);
		}

		if (updateRecentStickers) {
			api->requestRecentStickersForce(true);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if constexpr (ErrorWithId<FailCallback>) {
			fail(error.type(), requestId);
		} else if constexpr (ErrorWithoutId<FailCallback>) {
			fail(error.type());
		} else if constexpr (WithoutCallback<FailCallback>) {
			fail();
		} else {
			t_bad_callback(fail);
		}
	}).send();
}

template <typename DoneCallback, typename FailCallback>
mtpRequestId EditMessage(
		not_null<HistoryItem*> item,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail,
		std::optional<MTPInputMedia> inputMedia = std::nullopt) {
	const auto &text = item->originalText();
	const auto webpage = (!item->media() || !item->media()->webpage())
		? Data::WebPageDraft{ .removed = true }
		: Data::WebPageDraft{
			.id = item->media()->webpage()->id,
		};
	return EditMessage(
		item,
		text,
		webpage,
		options,
		std::forward<DoneCallback>(done),
		std::forward<FailCallback>(fail),
		inputMedia);
}

void EditMessageWithUploadedMedia(
		not_null<HistoryItem*> item,
		SendOptions options,
		MTPInputMedia media) {
	const auto done = [=](Fn<void()> applyUpdates) {
		if (item) {
			item->removeFromSharedMediaIndex();
			item->clearSavedMedia();
			item->setIsLocalUpdateMedia(true);
			applyUpdates();
			item->setIsLocalUpdateMedia(false);
		}
	};
	const auto fail = [=](const QString &error) {
		const auto session = &item->history()->session();
		const auto notModified = (error == u"MESSAGE_NOT_MODIFIED"_q);
		const auto mediaInvalid = (error == u"MEDIA_NEW_INVALID"_q);
		if (notModified || mediaInvalid) {
			item->returnSavedMedia();
			session->data().sendHistoryChangeNotifications();
			if (mediaInvalid) {
				Ui::show(
					Ui::MakeInformBox(tr::lng_edit_media_invalid_file()),
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
	const auto empty = [] {};
	EditMessage(item, options, empty, empty);
}

void EditMessageWithUploadedDocument(
		HistoryItem *item,
		RemoteFileInfo info,
		SendOptions options) {
	if (!item || !item->media() || !item->media()->document()) {
		return;
	}
	EditMessageWithUploadedMedia(
		item,
		options,
		PrepareUploadedDocument(item, std::move(info)));
}

void EditMessageWithUploadedPhoto(
		HistoryItem *item,
		RemoteFileInfo info,
		SendOptions options) {
	if (!item || !item->media() || !item->media()->photo()) {
		return;
	}
	EditMessageWithUploadedMedia(
		item,
		options,
		PrepareUploadedPhoto(item, std::move(info)));
}

mtpRequestId EditCaption(
		not_null<HistoryItem*> item,
		const TextWithEntities &caption,
		SendOptions options,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	return EditMessage(
		item,
		caption,
		Data::WebPageDraft(),
		options,
		done,
		fail);
}

mtpRequestId EditTextMessage(
		not_null<HistoryItem*> item,
		const TextWithEntities &caption,
		Data::WebPageDraft webpage,
		SendOptions options,
		Fn<void(mtpRequestId requestId)> done,
		Fn<void(const QString &, mtpRequestId requestId)> fail) {
	const auto callback = [=](Fn<void()> applyUpdates, mtpRequestId id) {
		applyUpdates();
		done(id);
	};
	return EditMessage(item, caption, webpage, options, callback, fail);
}

} // namespace Api
