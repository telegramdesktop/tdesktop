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
#include "base/random.h"
#include "ui/boxes/confirm_box.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/scheduled_messages.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_web_page.h"
#include "history/view/controls/history_view_compose_media_edit_manager.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "boxes/abstract_box.h" // Ui::show().

namespace Api {
namespace {

using namespace rpl::details;

template <typename T>
constexpr auto WithId
	= is_callable_plain_v<T, Fn<void()>, mtpRequestId>;
template <typename T>
constexpr auto WithoutId
	= is_callable_plain_v<T, Fn<void()>>;
template <typename T>
constexpr auto WithoutCallback
	= is_callable_plain_v<T>;
template <typename T>
constexpr auto ErrorWithId
	= is_callable_plain_v<T, QString, mtpRequestId>;
template <typename T>
constexpr auto ErrorWithoutId
	= is_callable_plain_v<T, QString>;

template <typename DoneCallback, typename FailCallback>
mtpRequestId SuggestMessage(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		Data::WebPageDraft webpage,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail) {
	Expects(options.suggest.exists);
	Expects(!options.scheduled);

	const auto session = &item->history()->session();
	const auto api = &session->api();

	const auto thread = item->history()->amMonoforumAdmin()
		? item->savedSublist()
		: (Data::Thread*)item->history();
	auto action = SendAction(thread, options);
	action.replyTo = FullReplyTo{
		.messageId = item->fullId(),
		.monoforumPeerId = (item->history()->amMonoforumAdmin()
			? item->sublistPeerId()
			: PeerId()),
	};

	auto message = MessageToSend(std::move(action));
	message.textWithTags = TextWithTags{
		textWithEntities.text,
		TextUtilities::ConvertEntitiesToTextTags(textWithEntities.entities)
	};
	message.webPage = webpage;
	api->sendMessage(std::move(message));

	const auto requestId = -1;
	crl::on_main(session, [=] {
		const auto type = u"MESSAGE_NOT_MODIFIED"_q;
		if constexpr (ErrorWithId<FailCallback>) {
			fail(type, requestId);
		} else if constexpr (ErrorWithoutId<FailCallback>) {
			fail(type);
		} else if constexpr (WithoutCallback<FailCallback>) {
			fail();
		} else {
			t_bad_callback(fail);
		}
	});
	return requestId;
}

template <typename DoneCallback, typename FailCallback>
mtpRequestId SuggestMedia(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		Data::WebPageDraft webpage,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail,
		std::optional<MTPInputMedia> inputMedia) {
	Expects(options.suggest.exists);
	Expects(!options.scheduled);

	const auto session = &item->history()->session();
	const auto api = &session->api();

	const auto text = textWithEntities.text;
	const auto sentEntities = EntitiesToMTP(
		session,
		textWithEntities.entities,
		ConvertOption::SkipLocal);

	const auto updateRecentStickers = inputMedia
		? Api::HasAttachedStickers(*inputMedia)
		: false;

	const auto emptyFlag = MTPmessages_SendMedia::Flag(0);
	auto replyTo = FullReplyTo{
		.messageId = item->fullId(),
		.monoforumPeerId = (item->history()->amMonoforumAdmin()
			? item->sublistPeerId()
			: PeerId()),
	};
	const auto flags = emptyFlag
		| MTPmessages_SendMedia::Flag::f_reply_to
		| MTPmessages_SendMedia::Flag::f_suggested_post
		| (((!webpage.removed && !webpage.url.isEmpty() && webpage.invert)
			|| options.invertCaption)
			? MTPmessages_SendMedia::Flag::f_invert_media
			: emptyFlag)
		| (!sentEntities.v.isEmpty()
			? MTPmessages_SendMedia::Flag::f_entities
			: emptyFlag)
		| (options.starsApproved
			? MTPmessages_SendMedia::Flag::f_allow_paid_stars
			: emptyFlag);
	const auto randomId = base::RandomValue<uint64>();
	return api->request(MTPmessages_SendMedia(
		MTP_flags(flags),
		item->history()->peer->input,
		ReplyToForMTP(item->history(), replyTo),
		inputMedia.value_or(Data::WebPageForMTP(webpage, text.isEmpty())),
		MTP_string(text),
		MTP_long(randomId),
		MTPReplyMarkup(),
		sentEntities,
		MTPint(), // schedule_date
		MTPInputPeer(), // send_as
		MTPInputQuickReplyShortcut(), // quick_reply_shortcut
		MTPlong(), // effect
		MTP_long(options.starsApproved),
		Api::SuggestToMTP(options.suggest)
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
			api->requestSpecialStickersForce(false, false, true);
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
mtpRequestId SuggestMessageOrMedia(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		Data::WebPageDraft webpage,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail,
		std::optional<MTPInputMedia> inputMedia) {
	const auto wasMedia = item->media();
	if (!inputMedia && wasMedia && wasMedia->allowsEditCaption()) {
		if (const auto photo = wasMedia->photo()) {
			inputMedia = MTP_inputMediaPhoto(
				MTP_flags(0),
				photo->mtpInput(),
				MTPint()); // ttl_seconds
		} else if (const auto document = wasMedia->document()) {
			inputMedia = MTP_inputMediaDocument(
				MTP_flags(0),
				document->mtpInput(),
				MTPInputPhoto(), // video_cover
				MTPint(), // video_timestamp
				MTPint(), // ttl_seconds
				MTPstring()); // query
		}
	}
	if (inputMedia) {
		return SuggestMedia(
			item,
			textWithEntities,
			webpage,
			options,
			std::move(done),
			std::move(fail),
			inputMedia);
	}
	return SuggestMessage(
		item,
		textWithEntities,
		webpage,
		options,
		std::move(done),
		std::move(fail));
}

template <typename DoneCallback, typename FailCallback>
mtpRequestId EditMessage(
		not_null<HistoryItem*> item,
		const TextWithEntities &textWithEntities,
		Data::WebPageDraft webpage,
		SendOptions options,
		DoneCallback &&done,
		FailCallback &&fail,
		std::optional<MTPInputMedia> inputMedia = std::nullopt) {
	if (item->computeSuggestionActions()
		== SuggestionActions::AcceptAndDecline) {
		return SuggestMessageOrMedia(
			item,
			textWithEntities,
			webpage,
			options,
			std::move(done),
			std::move(fail),
			inputMedia);
	}

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
		| (((!webpage.removed && !webpage.url.isEmpty() && webpage.invert)
			|| options.invertCaption)
			? MTPmessages_EditMessage::Flag::f_invert_media
			: emptyFlag)
		| (!sentEntities.v.isEmpty()
			? MTPmessages_EditMessage::Flag::f_entities
			: emptyFlag)
		| (options.scheduled
			? MTPmessages_EditMessage::Flag::f_schedule_date
			: emptyFlag)
		| (item->isBusinessShortcut()
			? MTPmessages_EditMessage::Flag::f_quick_reply_shortcut_id
			: emptyFlag);

	const auto id = item->isScheduled()
		? session->scheduledMessages().lookupId(item)
		: item->isBusinessShortcut()
		? session->data().shortcutMessages().lookupId(item)
		: item->id;
	return api->request(MTPmessages_EditMessage(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(id),
		MTP_string(text),
		inputMedia.value_or(Data::WebPageForMTP(webpage, text.isEmpty())),
		MTPReplyMarkup(),
		sentEntities,
		MTP_int(options.scheduled),
		MTP_int(item->shortcutId())
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
			api->requestSpecialStickersForce(false, false, true);
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
		: Data::WebPageDraft::FromItem(item);
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
	options.invertCaption = item->invertMedia();
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
		Fn<void(const QString &error, mtpRequestId requestId)> fail,
		bool spoilered) {
	const auto media = item->media();
	if (media
		&& HistoryView::MediaEditManager::CanBeSpoilered(item)
		&& spoilered != media->hasSpoiler()) {
		auto takeInputMedia = Fn<std::optional<MTPInputMedia>()>(nullptr);
		auto takeFileReference = Fn<QByteArray()>(nullptr);
		if (const auto photo = media->photo()) {
			using Flag = MTPDinputMediaPhoto::Flag;
			const auto flags = Flag()
				| (media->ttlSeconds() ? Flag::f_ttl_seconds : Flag())
				| (spoilered ? Flag::f_spoiler : Flag());
			takeInputMedia = [=] {
				return MTP_inputMediaPhoto(
					MTP_flags(flags),
					photo->mtpInput(),
					MTP_int(media->ttlSeconds()));
			};
			takeFileReference = [=] { return photo->fileReference(); };
		} else if (const auto document = media->document()) {
			using Flag = MTPDinputMediaDocument::Flag;
			const auto videoCover = media->videoCover();
			const auto videoTimestamp = media->videoTimestamp();
			const auto flags = Flag()
				| (media->ttlSeconds() ? Flag::f_ttl_seconds : Flag())
				| (spoilered ? Flag::f_spoiler : Flag())
				| (videoTimestamp ? Flag::f_video_timestamp : Flag())
				| (videoCover ? Flag::f_video_cover : Flag());
			takeInputMedia = [=] {
				return MTP_inputMediaDocument(
					MTP_flags(flags),
					document->mtpInput(),
					(videoCover
						? videoCover->mtpInput()
						: MTPInputPhoto()),
					MTP_int(media->ttlSeconds()),
					MTP_int(videoTimestamp),
					MTPstring()); // query
			};
			takeFileReference = [=] { return document->fileReference(); };
		}

		const auto usedFileReference = takeFileReference
			? takeFileReference()
			: QByteArray();
		const auto origin = item->fullId();
		const auto api = &item->history()->session().api();
		const auto performRequest = [=](
				const auto &repeatRequest,
				mtpRequestId originalRequestId) -> mtpRequestId {
			const auto handleReference = [=](
					const QString &error,
					mtpRequestId requestId) {
				if (error.startsWith(u"FILE_REFERENCE_"_q)) {
					api->refreshFileReference(origin, [=](const auto &) {
						if (takeFileReference &&
							(takeFileReference() != usedFileReference)) {
							repeatRequest(
								repeatRequest,
								originalRequestId
									? originalRequestId
									: requestId);
						} else {
							fail(error, requestId);
						}
					});
				} else {
					fail(error, requestId);
				}
			};
			const auto callback = [=](
					Fn<void()> applyUpdates,
					mtpRequestId requestId) {
				applyUpdates();
				done(originalRequestId ? originalRequestId : requestId);
			};
			const auto requestId = EditMessage(
				item,
				caption,
				webpage,
				options,
				callback,
				handleReference,
				takeInputMedia ? takeInputMedia() : std::nullopt);
			return originalRequestId ? originalRequestId : requestId;
		};
		return performRequest(performRequest, 0);
	}

	const auto callback = [=](Fn<void()> applyUpdates, mtpRequestId id) {
		applyUpdates();
		done(id);
	};
	return EditMessage(
		item,
		caption,
		webpage,
		options,
		callback,
		fail,
		std::nullopt);
}

void EditTodoList(
		not_null<HistoryItem*> item,
		const TodoListData &data,
		SendOptions options,
		Fn<void(mtpRequestId requestId)> done,
		Fn<void(const QString &error, mtpRequestId requestId)> fail) {
	const auto callback = [=](Fn<void()> applyUpdates, mtpRequestId id) {
		applyUpdates();
		done(id);
	};
	EditMessage(
		item,
		options,
		callback,
		fail,
		MTP_inputMediaTodo(TodoListDataToMTP(&data)));
}

} // namespace Api
