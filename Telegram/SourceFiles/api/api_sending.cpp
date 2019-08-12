/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_sending.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_channel.h" // ChannelData::addsSignature.
#include "data/data_user.h" // App::peerName(UserData*).
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "history/history.h"
#include "history/history_message.h" // NewMessageFlags.
#include "chat_helpers/message_field.h" // ConvertTextTagsToEntities.
#include "ui/text/text_entity.h" // TextWithEntities.
#include "main/main_session.h"
#include "mainwidget.h"
#include "apiwrap.h"

namespace Api {
namespace {

template <typename MediaData>
void SendExistingMedia(
		Api::MessageToSend &&message,
		not_null<MediaData*> media,
		const MTPInputMedia &inputMedia,
		Data::FileOrigin origin) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto newId = FullMsgId(
		peerToChannel(peer->id),
		session->data().nextLocalMessageId());
	const auto randomId = rand_value<uint64>();

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	auto clientFlags = NewMessageClientFlags();
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (message.action.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = message.action.options.silent
		|| (channelPost && session->data().notifySilentPosts(peer));
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : session->userId();
	auto messagePostAuthor = channelPost
		? App::peerName(session->user())
		: QString();

	auto caption = TextWithEntities{
		message.textWithTags.text,
		ConvertTextTagsToEntities(message.textWithTags.tags)
	};
	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}
	const auto replyTo = message.action.replyTo;
	const auto captionText = caption.text;

	if (message.action.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}

	session->data().registerMessageRandomId(randomId, newId);

	history->addNewLocalMessage(
		newId.msg,
		flags,
		clientFlags,
		0,
		replyTo,
		HistoryItem::NewMessageDate(message.action.options.scheduled),
		messageFromId,
		messagePostAuthor,
		media,
		caption,
		MTPReplyMarkup());

	auto failHandler = std::make_shared<Fn<void(const RPCError&, QByteArray)>>();
	auto performRequest = [=] {
		const auto usedFileReference = media->fileReference();
		history->sendRequestId = api->request(MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			peer->input,
			MTP_int(replyTo),
			inputMedia,
			MTP_string(captionText),
			MTP_long(randomId),
			MTPReplyMarkup(),
			sentEntities,
			MTP_int(message.action.options.scheduled)
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result, randomId);
		}).fail([=](const RPCError &error) {
			(*failHandler)(error, usedFileReference);
		}).afterRequest(history->sendRequestId
		).send();
	};
	*failHandler = [=](const RPCError &error, QByteArray usedFileReference) {
		if (error.code() == 400
			&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
			api->refreshFileReference(origin, [=](const auto &result) {
				if (media->fileReference() != usedFileReference) {
					performRequest();
				} else {
					api->sendMessageFail(error, peer, newId);
				}
			});
		} else {
			api->sendMessageFail(error, peer, newId);
		}
	};
	performRequest();

	if (const auto main = App::main()) {
		main->finishForwarding(message.action);
	}
}

} // namespace

void SendExistingDocument(
		Api::MessageToSend &&message,
		not_null<DocumentData*> document) {
	SendExistingMedia(
		std::move(message),
		document,
		MTP_inputMediaDocument(
			MTP_flags(0),
			document->mtpInput(),
			MTPint()),
		document->stickerOrGifOrigin());

	if (document->sticker()) {
		if (const auto main = App::main()) {
			main->incrementSticker(document);
			document->session().data().notifyRecentStickersUpdated();
		}
	}
}

void SendExistingPhoto(
		Api::MessageToSend &&message,
		not_null<PhotoData*> photo) {
	SendExistingMedia(
		std::move(message),
		photo,
		MTP_inputMediaPhoto(
			MTP_flags(0),
			photo->mtpInput(),
			MTPint()),
		Data::FileOrigin());
}

} // namespace Api
