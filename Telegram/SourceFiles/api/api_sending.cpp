/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_sending.h"

#include "api/api_text_entities.h"
#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_channel.h" // ChannelData::addsSignature.
#include "data/data_user.h" // UserData::name
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "history/history.h"
#include "history/history_message.h" // NewMessageFlags.
#include "chat_helpers/message_field.h" // ConvertTextTagsToEntities.
#include "ui/text/text_entity.h" // TextWithEntities.
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "app.h"

namespace Api {
namespace {

void InnerFillMessagePostFlags(
		const Api::SendOptions &options,
		not_null<PeerData*> peer,
		MTPDmessage::Flags &flags) {
	const auto channelPost = peer->isChannel() && !peer->isMegagroup();
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
		return;
	}
	flags |= MTPDmessage::Flag::f_post;
	// Don't display views and author of a new post when it's scheduled.
	if (options.scheduled) {
		return;
	}
	flags |= MTPDmessage::Flag::f_views;
	if (peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
}

template <typename MediaData>
void SendExistingMedia(
		Api::MessageToSend &&message,
		not_null<MediaData*> media,
		Fn<MTPInputMedia()> inputMedia,
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
	InnerFillMessagePostFlags(message.action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : session->userId();
	auto messagePostAuthor = channelPost ? session->user()->name : QString();

	auto caption = TextWithEntities{
		message.textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(message.textWithTags.tags)
	};
	TextUtilities::Trim(caption);
	auto sentEntities = EntitiesToMTP(
		session,
		caption.entities,
		ConvertOption::SkipLocal);
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
		auto &histories = history->owner().histories();
		const auto requestType = Data::Histories::RequestType::Send;
		histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
			const auto usedFileReference = media->fileReference();
			history->sendRequestId = api->request(MTPmessages_SendMedia(
				MTP_flags(sendFlags),
				peer->input,
				MTP_int(replyTo),
				inputMedia(),
				MTP_string(captionText),
				MTP_long(randomId),
				MTPReplyMarkup(),
				sentEntities,
				MTP_int(message.action.options.scheduled)
			)).done([=](const MTPUpdates &result) {
				api->applyUpdates(result, randomId);
				finish();
			}).fail([=](const RPCError &error) {
				(*failHandler)(error, usedFileReference);
				finish();
			}).afterRequest(history->sendRequestId
			).send();
			return history->sendRequestId;
		});
	};
	*failHandler = [=](const RPCError &error, QByteArray usedFileReference) {
		if (error.code() == 400
			&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
			api->refreshFileReference(origin, [=](const auto &result) {
				if (media->fileReference() != usedFileReference) {
					performRequest();
				} else {
					api->sendMessageFail(error, peer, randomId, newId);
				}
			});
		} else {
			api->sendMessageFail(error, peer, randomId, newId);
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
	const auto inputMedia = [=] {
		return MTP_inputMediaDocument(
			MTP_flags(0),
			document->mtpInput(),
			MTPint());
	};
	SendExistingMedia(
		std::move(message),
		document,
		inputMedia,
		document->stickerOrGifOrigin());

	if (document->sticker()) {
		if (const auto main = App::main()) {
			main->incrementSticker(document);
			document->owner().notifyRecentStickersUpdated();
		}
	}
}

void SendExistingPhoto(
		Api::MessageToSend &&message,
		not_null<PhotoData*> photo) {
	const auto inputMedia = [=] {
		return MTP_inputMediaPhoto(
			MTP_flags(0),
			photo->mtpInput(),
			MTPint());
	};
	SendExistingMedia(
		std::move(message),
		photo,
		inputMedia,
		Data::FileOrigin());
}

bool SendDice(Api::MessageToSend &message) {
	const auto full = message.textWithTags.text.midRef(0).trimmed();
	auto length = 0;
	if (!Ui::Emoji::Find(full.data(), full.data() + full.size(), &length)
		|| length != full.size()) {
		return false;
	}
	auto &account = message.action.history->session().account();
	auto &config = account.appConfig();
	static const auto hardcoded = std::vector<QString>{
		QString::fromUtf8("\xF0\x9F\x8E\xB2"),
		QString::fromUtf8("\xF0\x9F\x8E\xAF")
	};
	const auto list = config.get<std::vector<QString>>(
		"emojies_send_dice",
		hardcoded);
	const auto emoji = full.toString();
	if (!ranges::contains(list, emoji)) {
		return false;
	}
	const auto history = message.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	message.textWithTags = TextWithTags();
	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto newId = FullMsgId(
		peerToChannel(peer->id),
		session->data().nextLocalMessageId());
	const auto randomId = rand_value<uint64>();

	auto &histories = history->owner().histories();
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
	InnerFillMessagePostFlags(message.action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : session->userId();
	auto messagePostAuthor = channelPost ? session->user()->name : QString();
	const auto replyTo = message.action.replyTo;

	if (message.action.options.scheduled) {
		flags |= MTPDmessage::Flag::f_from_scheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	} else {
		clientFlags |= MTPDmessage_ClientFlag::f_local_history_entry;
	}

	session->data().registerMessageRandomId(randomId, newId);

	history->addNewMessage(
		MTP_message(
			MTP_flags(flags),
			MTP_int(newId.msg),
			MTP_int(messageFromId),
			peerToMTP(history->peer->id),
			MTPMessageFwdHeader(),
			MTP_int(0),
			MTP_int(replyTo),
			MTP_int(HistoryItem::NewMessageDate(
				message.action.options.scheduled)),
			MTP_string(),
			MTP_messageMediaDice(MTP_int(0), MTP_string(emoji)),
			MTPReplyMarkup(),
			MTP_vector<MTPMessageEntity>(),
			MTP_int(1),
			MTPint(),
			MTP_string(messagePostAuthor),
			MTPlong(),
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>()),
		clientFlags,
		NewMessageType::Unread);

	const auto requestType = Data::Histories::RequestType::Send;
	histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
		history->sendRequestId = api->request(MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			peer->input,
			MTP_int(replyTo),
			MTP_inputMediaDice(MTP_string(emoji)),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTP_vector<MTPMessageEntity>(),
			MTP_int(message.action.options.scheduled)
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result, randomId);
			finish();
		}).fail([=](const RPCError &error) {
			api->sendMessageFail(error, peer, randomId, newId);
			finish();
		}).afterRequest(history->sendRequestId
		).send();
		return history->sendRequestId;
	});
	return true;
}

void FillMessagePostFlags(
		const Api::SendAction &action,
		not_null<PeerData*> peer,
		MTPDmessage::Flags &flags) {
	InnerFillMessagePostFlags(action.options, peer, flags);
}

} // namespace Api
