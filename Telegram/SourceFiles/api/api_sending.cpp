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
#include "ui/text/text_entity.h" // TextWithEntities.
#include "main/main_session.h"
#include "mainwidget.h"
#include "apiwrap.h"

namespace Api {
namespace {

template <typename MediaData>
void SendExistingMedia(
		not_null<History*> history,
		not_null<MediaData*> media,
		const MTPInputMedia &inputMedia,
		Data::FileOrigin origin,
		TextWithEntities caption,
		MsgId replyToId,
		bool silent) {
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	auto options = ApiWrap::SendOptions(history);
	options.clearDraft = false;
	options.replyTo = replyToId;
	options.generateLocal = true;
	options.silent = silent;

	api->sendAction(options);

	const auto newId = FullMsgId(peerToChannel(peer->id), clientMsgId());
	const auto randomId = rand_value<uint64>();

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	const auto silentPost = options.silent
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

	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}
	const auto replyTo = options.replyTo;
	const auto captionText = caption.text;

	session->data().registerMessageRandomId(randomId, newId);

	history->addNewLocalMessage(
		newId.msg,
		flags,
		0,
		replyTo,
		base::unixtime::now(),
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
			sentEntities
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
		main->finishForwarding(history, options.silent);
	}
}

} // namespace

void SendExistingDocument(
		not_null<History*> history,
		not_null<DocumentData*> document,
		bool silent) {
	SendExistingDocument(history, document, {}, 0, silent);
}

void SendExistingDocument(
		not_null<History*> history,
		not_null<DocumentData*> document,
		TextWithEntities caption,
		MsgId replyToId,
		bool silent) {
	SendExistingMedia(
		history,
		document,
		MTP_inputMediaDocument(
			MTP_flags(0),
			document->mtpInput(),
			MTPint()),
		document->stickerOrGifOrigin(),
		caption,
		replyToId,
		silent);

	if (document->sticker()) {
		if (const auto main = App::main()) {
			main->incrementSticker(document);
			document->session().data().notifyRecentStickersUpdated();
		}
	}
}

void SendExistingPhoto(
		not_null<History*> history,
		not_null<PhotoData*> photo,
		bool silent) {
	SendExistingPhoto(history, photo, {}, 0, silent);
}

void SendExistingPhoto(
		not_null<History*> history,
		not_null<PhotoData*> photo,
		TextWithEntities caption,
		MsgId replyToId,
		bool silent) {
	SendExistingMedia(
		history,
		photo,
		MTP_inputMediaPhoto(
			MTP_flags(0),
			photo->mtpInput(),
			MTPint()),
		Data::FileOrigin(),
		caption,
		replyToId,
		silent);
}

} // namespace Api
