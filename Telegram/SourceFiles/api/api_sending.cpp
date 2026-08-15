/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_sending.h"

#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_channel.h" // ChannelData::addsSignature.
#include "data/data_user.h" // UserData::name
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/components/ephemeral_messages.h"
#include "data/components/welcome_messages.h"
#include "data/stickers/data_stickers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h" // NewMessageFlags.
#include "chat_helpers/message_field.h" // ConvertTextTagsToEntities.
#include "chat_helpers/stickers_dice_pack.h" // DicePacks::kDiceString.
#include "ui/text/text_entity.h" // TextWithEntities.
#include "ui/item_text_options.h" // Ui::ItemTextOptions.
#include "ui/chat/attach/attach_prepare.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "storage/localimageloader.h"
#include "storage/file_upload.h"
#include "mainwidget.h"
#include "apiwrap.h"

namespace Api {
namespace {

void InnerFillMessagePostFlags(
		const SendOptions &options,
		not_null<PeerData*> peer,
		MessageFlags &flags) {
	if (ShouldSendSilent(peer, options)) {
		flags |= MessageFlag::Silent;
	}
	if (!peer->amAnonymous()
		|| (!peer->isBroadcast()
			&& options.sendAs
			&& options.sendAs != peer)) {
		flags |= MessageFlag::HasFromId;
	}
	const auto channel = peer->asBroadcast();
	if (!channel) {
		return;
	}
	flags |= MessageFlag::Post;
	// Don't display views and author of a new post when it's scheduled.
	if (options.scheduled) {
		return;
	}
	flags |= MessageFlag::HasViews;
	if (channel->addsSignature()) {
		flags |= MessageFlag::HasPostAuthor;
	}
}

void SendSimpleMedia(SendAction action, MTPInputMedia inputMedia) {
	const auto history = action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	action.clearDraft = false;
	action.generateLocal = false;
	api->sendAction(action);

	if (!action.options.scheduled
		&& !action.options.shortcutId
		&& session->ephemeralMessages().sendSimpleMedia(
			history,
			action.replyTo,
			inputMedia)) {
		api->finishForwarding(action);
		return;
	}

	if (action.replyTo.messageId
		&& !IsServerMsgId(action.replyTo.messageId.msg)
		&& !session->data().message(action.replyTo.messageId)) {
		action.replyTo = {
			.messageId = (action.replyTo.topicRootId
				? FullMsgId(peer->id, action.replyTo.topicRootId)
				: FullMsgId()),
			.topicRootId = action.replyTo.topicRootId,
			.monoforumPeerId = action.replyTo.monoforumPeerId,
		};
	}

	const auto randomId = base::RandomValue<uint64>();

	auto flags = NewMessageFlags(peer);
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	InnerFillMessagePostFlags(action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	const auto messagePostAuthor = peer->isBroadcast()
		? session->user()->name()
		: QString();
	const auto starsPaid = std::min(
		peer->starsPerMessageChecked(),
		action.options.starsApproved);
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
		if (action.options.scheduleRepeatPeriod) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_repeat_period;
		}
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
		sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
	}
	if (action.options.effectId) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
	}
	if (action.options.suggest) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_suggested_post;
	}
	if (action.options.invertCaption) {
		flags |= MessageFlag::InvertMedia;
		sendFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
	}
	if (starsPaid) {
		action.options.starsApproved -= starsPaid;
		sendFlags |= MTPmessages_SendMedia::Flag::f_allow_paid_stars;
	}

	auto &histories = history->owner().histories();
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(sendFlags),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			std::move(inputMedia),
			MTPstring(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPvector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(session, action.options.shortcutId),
			MTP_long(action.options.effectId),
			MTP_long(starsPaid),
			SuggestToMTP(action.options.suggest)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		api->sendMessageFail(error, peer, randomId);
	});

	api->finishForwarding(action);
}

template <typename MediaData>
void SendExistingMedia(
		MessageToSend &&message,
		not_null<MediaData*> media,
		Fn<MTPInputMedia()> inputMedia,
		Data::FileOrigin origin,
		std::optional<MsgId> localMessageId) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();
	const auto welcomeTemplate = message.action.options.welcomeTemplate;

	if (welcomeTemplate
		&& session->welcomeMessages().count(history)
			>= Data::WelcomeMessagesLimit(session)) {
		return;
	}

	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto newId = FullMsgId(
		peer->id,
		localMessageId
			? (*localMessageId)
			: session->data().nextLocalMessageId());
	const auto randomId = welcomeTemplate
		? uint64(0)
		: base::RandomValue<uint64>();
	auto &action = message.action;

	auto flags = NewMessageFlags(peer);
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (welcomeTemplate) {
		flags &= ~MessageFlag::Outgoing;
		flags |= MessageFlag::FakeHistoryItem;
	}
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	if (!welcomeTemplate
		&& !action.options.scheduled
		&& !action.options.shortcutId
		&& session->ephemeralMessages().wouldSendMedia(
			peer,
			action.replyTo,
			message.textWithTags.text)) {
		flags |= MessageFlag::Ephemeral;
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	InnerFillMessagePostFlags(action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
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
	const auto captionText = caption.text;
	const auto starsPaid = std::min(
		peer->starsPerMessageChecked(),
		action.options.starsApproved);
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
		if (action.options.scheduleRepeatPeriod) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_repeat_period;
		}
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
		sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
	}
	if (action.options.effectId) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
	}
	if (action.options.suggest) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_suggested_post;
	}
	if (action.options.invertCaption) {
		flags |= MessageFlag::InvertMedia;
		sendFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
	}
	if (starsPaid) {
		action.options.starsApproved -= starsPaid;
		sendFlags |= MTPmessages_SendMedia::Flag::f_allow_paid_stars;
	}

	const auto item = history->addNewLocalMessage({
		.id = newId.msg,
		.flags = flags,
		.from = NewMessageFromId(action),
		.replyTo = action.replyTo,
		.date = NewMessageDate(action.options),
		.scheduleRepeatPeriod = action.options.scheduleRepeatPeriod,
		.shortcutId = action.options.shortcutId,
		.starsPaid = starsPaid,
		.postAuthor = NewMessagePostAuthor(action),
		.effectId = action.options.effectId,
		.suggest = HistoryMessageSuggestInfo(action.options),
		.mediaSpoiler = action.options.mediaSpoiler,
	}, media, caption);

	if (welcomeTemplate) {
		auto &welcome = session->welcomeMessages();
		welcome.appendSending(item);
		welcome.sendMedia(item, inputMedia(), origin, inputMedia);
		api->finishForwarding(action);
		return;
	}

	session->data().registerMessageRandomId(randomId, newId);

	if (session->ephemeralMessages().sendMedia(
			item,
			inputMedia(),
			origin,
			inputMedia)) {
		api->finishForwarding(action);
		return;
	}

	const auto performRequest = [=](const auto &repeatRequest) -> void {
		auto &histories = history->owner().histories();
		const auto session = &history->session();
		const auto usedFileReference = media->fileReference();
		histories.sendPreparedMessage(
			history,
			action.replyTo,
			randomId,
			Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
				MTP_flags(sendFlags),
				peer->input(),
				Data::Histories::ReplyToPlaceholder(),
				inputMedia(),
				MTP_string(captionText),
				MTP_long(randomId),
				MTPReplyMarkup(),
				sentEntities,
				MTP_int(action.options.scheduled),
				MTP_int(action.options.scheduleRepeatPeriod),
				(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
				Data::ShortcutIdToMTP(session, action.options.shortcutId),
				MTP_long(action.options.effectId),
				MTP_long(starsPaid),
				SuggestToMTP(action.options.suggest)
			), [=](const MTPUpdates &result, const MTP::Response &response) {
		}, [=](const MTP::Error &error, const MTP::Response &response) {
			if (error.code() == 400
				&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
				api->refreshFileReference(origin, [=](const auto &result) {
					if (media->fileReference() != usedFileReference) {
						repeatRequest(repeatRequest);
					} else {
						api->sendMessageFail(error, peer, randomId, newId);
					}
				});
			} else {
				api->sendMessageFail(error, peer, randomId, newId);
			}
		});
	};
	performRequest(performRequest);

	api->finishForwarding(action);
}

struct MusicSendRequestItem {
	MusicSelectionItem item;
	not_null<HistoryItem*> localItem;
	FullMsgId localId;
	uint64 randomId = 0;
	TextWithEntities caption;
};

struct MusicRefreshItem {
	not_null<DocumentData*> document;
	Data::FileOrigin origin;
	QByteArray usedFileReference;
};

struct MusicSelectionState {
	SendAction action;
	std::vector<MusicSelectionItem> items;
	TextWithEntities caption;
	int offset = 0;
};

[[nodiscard]] MTPInputMedia PrepareMusicInputMedia(
		const MusicSelectionItem &item,
		bool spoiler) {
	using Flag = MTPDinputMediaDocument::Flag;
	return MTP_inputMediaDocument(
		MTP_flags(spoiler
			? Flag::f_spoiler
			: Flag(0)),
		item.document->mtpInput(),
		MTPInputPhoto(), // video_cover
		MTPint(), // ttl_seconds
		MTPint(), // video_timestamp
		MTPstring()); // query
}

[[nodiscard]] MTPInputSingleMedia PrepareMusicInputSingleMedia(
		not_null<Main::Session*> session,
		const MusicSendRequestItem &item,
		bool spoiler) {
	const auto entities = EntitiesToMTP(
		session,
		item.caption.entities,
		ConvertOption::SkipLocal);
	using Flag = MTPDinputSingleMedia::Flag;
	return MTP_inputSingleMedia(
		MTP_flags(!entities.v.isEmpty()
			? Flag::f_entities
			: Flag(0)),
		PrepareMusicInputMedia(item.item, spoiler),
		MTP_long(item.randomId),
		MTP_string(item.caption.text),
		entities);
}

void SendMusicSelectionBatch(
		SendAction &action,
		std::vector<MusicSelectionItem> items,
		TextWithEntities caption,
		Fn<void()> done) {
	Expects(!items.empty());

	const auto history = action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();
	const auto actionPtr = &action;
	const auto multi = (items.size() > 1);
	const auto groupId = multi ? base::RandomValue<uint64>() : uint64(0);

	auto flags = NewMessageFlags(peer);
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	if (!multi
		&& !action.options.scheduled
		&& !action.options.shortcutId
		&& session->ephemeralMessages().isEphemeralBotReply(
			action.replyTo.messageId)) {
		flags |= MessageFlag::Ephemeral;
	}
	InnerFillMessagePostFlags(action.options, peer, flags);
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
	}
	if (action.options.invertCaption) {
		flags |= MessageFlag::InvertMedia;
	}

	auto batchStarsPaid = 0;
	auto remainingStarsApproved = action.options.starsApproved;
	auto requests = std::vector<MusicSendRequestItem>();
	requests.reserve(items.size());
	for (auto i = 0; i != int(items.size()); ++i) {
		auto itemCaption = (i + 1 == int(items.size()))
			? caption
			: TextWithEntities();
		const auto newId = FullMsgId(
			peer->id,
			session->data().nextLocalMessageId());
		const auto randomId = base::RandomValue<uint64>();
		const auto messageStarsPaid = std::min(
			peer->starsPerMessageChecked(),
			remainingStarsApproved);
		remainingStarsApproved -= messageStarsPaid;
		batchStarsPaid += messageStarsPaid;

		session->data().registerMessageRandomId(randomId, newId);
		const auto localItem = history->addNewLocalMessage({
			.id = newId.msg,
			.flags = flags,
			.from = NewMessageFromId(action),
			.replyTo = action.replyTo,
			.date = NewMessageDate(action.options),
			.scheduleRepeatPeriod = action.options.scheduleRepeatPeriod,
			.shortcutId = action.options.shortcutId,
			.starsPaid = messageStarsPaid,
			.postAuthor = NewMessagePostAuthor(action),
			.groupedId = groupId,
			.effectId = action.options.effectId,
			.suggest = HistoryMessageSuggestInfo(action.options),
			.mediaSpoiler = action.options.mediaSpoiler,
		}, items[i].document, itemCaption);
		requests.push_back({
			.item = std::move(items[i]),
			.localItem = localItem,
			.localId = newId,
			.randomId = randomId,
			.caption = std::move(itemCaption),
		});
	}

	const auto failRequest = [=](const MTP::Error &error) {
		for (const auto &item : requests) {
			api->sendMessageFail(error, peer, item.randomId, item.localId);
		}
		if (done) {
			done();
		}
	};

	const auto refreshItems = [=] {
		auto result = std::vector<MusicRefreshItem>();
		result.reserve(requests.size());
		for (const auto &item : requests) {
			if (!item.item.origin) {
				continue;
			}
			result.push_back({
				.document = item.item.document,
				.origin = item.item.origin,
				.usedFileReference = item.item.document->fileReference(),
			});
		}
		return result;
	}();
	if (!multi) {
		const auto &item = requests.front();
		if (session->ephemeralMessages().sendMedia(
				item.localItem,
				PrepareMusicInputMedia(
					item.item,
					action.options.mediaSpoiler))) {
			if (done) {
				done();
			}
			return;
		}
	}

	const auto performRequest = [=](const auto &repeatRequest, bool refreshed)
			-> void {
		const auto sendAs = action.options.sendAs;
		const auto retryOrFail = [=](
				const MTP::Error &error,
				const MTP::Response &response) {
			if (refreshed
				|| (error.code() != 400)
				|| !error.type().startsWith(u"FILE_REFERENCE_"_q)
				|| refreshItems.empty()) {
				failRequest(error);
				return;
			}
			const auto changed = std::make_shared<bool>(false);
			const auto left = std::make_shared<int>(int(refreshItems.size()));
			for (const auto &refresh : refreshItems) {
				api->refreshFileReference(refresh.origin, [=](const auto &) {
					*changed = *changed
						|| (refresh.document->fileReference()
							!= refresh.usedFileReference);
					if (!--*left) {
						if (*changed) {
							repeatRequest(repeatRequest, true);
						} else {
							failRequest(error);
						}
					}
				});
			}
		};
		if (!multi) {
			const auto &item = requests.front();
			const auto inputMedia = PrepareMusicInputMedia(
				item.item,
				action.options.mediaSpoiler);
			const auto entities = EntitiesToMTP(
				session,
				item.caption.entities,
				ConvertOption::SkipLocal);
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (action.replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
			}
			if (ShouldSendSilent(peer, action.options)) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			if (!entities.v.isEmpty()) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
			}
			if (action.options.scheduled) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
				if (action.options.scheduleRepeatPeriod) {
					sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_repeat_period;
				}
			}
			if (action.options.shortcutId) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
			}
			if (sendAs) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
			}
			if (action.options.effectId) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
			}
			if (action.options.suggest) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_suggested_post;
			}
			if (action.options.invertCaption) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
			}
			if (batchStarsPaid) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_allow_paid_stars;
			}

			auto &histories = history->owner().histories();
			histories.sendPreparedMessage(
				history,
				action.replyTo,
				item.randomId,
				Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
					MTP_flags(sendFlags),
					peer->input(),
					Data::Histories::ReplyToPlaceholder(),
					(action.options.price
						? MTPInputMedia(MTP_inputMediaPaidMedia(
							MTP_flags(0),
							MTP_long(action.options.price),
							MTP_vector<MTPInputMedia>(1, inputMedia),
							MTPstring()))
						: inputMedia),
					MTP_string(item.caption.text),
					MTP_long(item.randomId),
					MTPReplyMarkup(),
					entities,
					MTP_int(action.options.scheduled),
					MTP_int(action.options.scheduleRepeatPeriod),
					(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
					Data::ShortcutIdToMTP(session, action.options.shortcutId),
					MTP_long(action.options.effectId),
					MTP_long(batchStarsPaid),
					SuggestToMTP(action.options.suggest)
				), [=](const MTPUpdates &result, const MTP::Response &response) {
				actionPtr->options.starsApproved -= batchStarsPaid;
				if (done) {
					done();
				}
			}, retryOrFail);
			return;
		}

		using Flag = MTPmessages_SendMultiMedia::Flag;
		const auto sendFlags = Flag(0)
			| (action.replyTo ? Flag::f_reply_to : Flag(0))
			| (ShouldSendSilent(peer, action.options)
				? Flag::f_silent
				: Flag(0))
			| (action.options.scheduled
				? Flag::f_schedule_date
				: Flag(0))
			| (sendAs ? Flag::f_send_as : Flag(0))
			| (action.options.shortcutId
				? Flag::f_quick_reply_shortcut
				: Flag(0))
			| (action.options.effectId ? Flag::f_effect : Flag(0))
			| (action.options.invertCaption
				? Flag::f_invert_media
				: Flag(0))
			| (batchStarsPaid ? Flag::f_allow_paid_stars : Flag(0));

		auto media = QVector<MTPInputSingleMedia>();
		media.reserve(requests.size());
		for (const auto &item : requests) {
			media.push_back(PrepareMusicInputSingleMedia(
				session,
				item,
				action.options.mediaSpoiler));
		}

		auto &histories = history->owner().histories();
		histories.sendPreparedMessage(
			history,
			action.replyTo,
			uint64(0),
			Data::Histories::PrepareMessage<MTPmessages_SendMultiMedia>(
				MTP_flags(sendFlags),
				peer->input(),
				Data::Histories::ReplyToPlaceholder(),
				MTP_vector<MTPInputSingleMedia>(std::move(media)),
				MTP_int(action.options.scheduled),
				(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
				Data::ShortcutIdToMTP(session, action.options.shortcutId),
				MTP_long(action.options.effectId),
				MTP_long(batchStarsPaid)
			), [=](const MTPUpdates &result, const MTP::Response &response) {
			actionPtr->options.starsApproved -= batchStarsPaid;
			if (done) {
				done();
			}
		}, retryOrFail);
	};
	performRequest(performRequest, false);
}

} // namespace

void SendExistingDocument(
		MessageToSend &&message,
		not_null<DocumentData*> document,
		std::optional<MsgId> localMessageId) {
	const auto inputMedia = [=] {
		return MTP_inputMediaDocument(
			MTP_flags(message.action.options.mediaSpoiler
				? MTPDinputMediaDocument::Flag::f_spoiler
				: MTPDinputMediaDocument::Flags(0)),
			document->mtpInput(),
			MTPInputPhoto(), // video_cover
			MTPint(), // ttl_seconds
			MTPint(), // video_timestamp
			MTPstring()); // query
	};
	SendExistingMedia(
		std::move(message),
		document,
		inputMedia,
		document->stickerOrGifOrigin(),
		std::move(localMessageId));

	if (document->sticker()) {
		document->owner().stickers().incrementSticker(document);
	}
}

void SendMusicSelection(
		MessageToSend &&message,
		std::vector<MusicSelectionItem> items) {
	if (items.empty()) {
		return;
	}

	auto caption = TextWithEntities{
		std::move(message.textWithTags.text),
		TextUtilities::ConvertTextTagsToEntities(message.textWithTags.tags)
	};
	TextUtilities::Trim(caption);

	const auto api = &message.action.history->session().api();

	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto state = std::make_shared<MusicSelectionState>(MusicSelectionState{
		.action = message.action,
		.items = std::move(items),
		.caption = std::move(caption),
	});
	const auto sendNext = [=](const auto &self) -> void {
		const auto count = int(state->items.size());
		if (state->offset == count) {
			api->finishForwarding(state->action);
			return;
		}
		const auto optionsRequireSingle = state->action.options.price
			|| state->action.options.scheduleRepeatPeriod
			|| state->action.options.suggest;
		const auto batchLimit = optionsRequireSingle
			? 1
			: Ui::MaxAlbumItems();
		const auto till = std::min(state->offset + batchLimit, count);
		auto batch = std::vector<MusicSelectionItem>();
		batch.reserve(till - state->offset);
		for (; state->offset != till; ++state->offset) {
			batch.push_back(std::move(state->items[state->offset]));
		}
		auto batchCaption = TextWithEntities();
		if (state->offset == count) {
			batchCaption = std::move(state->caption);
		}
		SendMusicSelectionBatch(
			state->action,
			std::move(batch),
			std::move(batchCaption),
			[=] { self(self); });
	};
	sendNext(sendNext);
}

void SendExistingPhoto(
		MessageToSend &&message,
		not_null<PhotoData*> photo,
		std::optional<MsgId> localMessageId) {
	const auto inputMedia = [=] {
		return MTP_inputMediaPhoto(
			MTP_flags(0),
			photo->mtpInput(),
			MTPint(), // ttl_seconds
			MTPInputDocument()); // video
	};
	SendExistingMedia(
		std::move(message),
		photo,
		inputMedia,
		Data::FileOrigin(),
		std::move(localMessageId));
}

bool SendDice(MessageToSend &message) {
	const auto full = QStringView(message.textWithTags.text).trimmed();
	auto length = 0;
	if (!Ui::Emoji::Find(full.data(), full.data() + full.size(), &length)
		|| length != full.size()
		|| !message.textWithTags.tags.isEmpty()) {
		return false;
	}
	const auto &config = message.action.history->session().appConfig();
	static const auto hardcoded = std::vector<QString>{
		Stickers::DicePacks::kDiceString,
		Stickers::DicePacks::kDartString,
		Stickers::DicePacks::kSlotString,
		Stickers::DicePacks::kFballString,
		Stickers::DicePacks::kFballString + QChar(0xFE0F),
		Stickers::DicePacks::kBballString,
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

	auto &action = message.action;
	api->sendAction(action);

	const auto newId = FullMsgId(
		peer->id,
		session->data().nextLocalMessageId());
	const auto randomId = base::RandomValue<uint64>();

	auto &histories = history->owner().histories();
	auto flags = NewMessageFlags(peer);
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	InnerFillMessagePostFlags(action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	if (action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
		if (action.options.scheduleRepeatPeriod) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_repeat_period;
		}
	}
	if (action.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;
		sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
	}
	if (action.options.effectId) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
	}
	if (action.options.suggest) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_suggested_post;
	}
	if (action.options.invertCaption) {
		flags |= MessageFlag::InvertMedia;
		sendFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
	}
	const auto starsPaid = std::min(
		peer->starsPerMessageChecked(),
		action.options.starsApproved);
	if (starsPaid) {
		action.options.starsApproved -= starsPaid;
		sendFlags |= MTPmessages_SendMedia::Flag::f_allow_paid_stars;
	}

	session->data().registerMessageRandomId(randomId, newId);

	auto seed = QByteArray(32, Qt::Uninitialized);
	base::RandomFill(bytes::make_detached_span(seed));
	const auto stake = action.options.stakeSeedHash.isEmpty()
		? 0
		: action.options.stakeNanoTon;
	history->addNewLocalMessage({
		.id = newId.msg,
		.flags = flags,
		.from = NewMessageFromId(action),
		.replyTo = action.replyTo,
		.date = NewMessageDate(action.options),
		.scheduleRepeatPeriod = action.options.scheduleRepeatPeriod,
		.shortcutId = action.options.shortcutId,
		.starsPaid = starsPaid,
		.postAuthor = NewMessagePostAuthor(action),
		.effectId = action.options.effectId,
		.suggest = HistoryMessageSuggestInfo(action.options),
	}, TextWithEntities(), MTP_messageMediaDice(
		MTP_flags(stake
			? MTPDmessageMediaDice::Flag::f_game_outcome
			: MTPDmessageMediaDice::Flag()),
		MTP_int(0),
		MTP_string(emoji),
		MTP_messages_emojiGameOutcome(
			MTP_bytes(seed),
			MTP_long(stake),
			MTP_long(0))));
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(sendFlags),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			(stake
				? MTP_inputMediaStakeDice(
					MTP_bytes(action.options.stakeSeedHash),
					MTP_long(stake),
					MTP_bytes(seed))
				: MTP_inputMediaDice(MTP_string(emoji))),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTP_vector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(session, action.options.shortcutId),
			MTP_long(action.options.effectId),
			MTP_long(starsPaid),
			SuggestToMTP(action.options.suggest)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		api->sendMessageFail(error, peer, randomId, newId);
	});
	api->finishForwarding(action);
	return true;
}

void SendLocation(SendAction action, float64 lat, float64 lon) {
	SendSimpleMedia(
		action,
		MTP_inputMediaGeoPoint(
			MTP_inputGeoPoint(
				MTP_flags(0),
				MTP_double(lat),
				MTP_double(lon),
				MTPint()))); // accuracy_radius
}

void SendVenue(SendAction action, Data::InputVenue venue) {
	SendSimpleMedia(
		action,
		MTP_inputMediaVenue(
			MTP_inputGeoPoint(
				MTP_flags(0),
				MTP_double(venue.lat),
				MTP_double(venue.lon),
				MTPint()), // accuracy_radius
			MTP_string(venue.title),
			MTP_string(venue.address),
			MTP_string(venue.provider),
			MTP_string(venue.id),
			MTP_string(venue.venueType)));
}

void FillMessagePostFlags(
		const SendAction &action,
		not_null<PeerData*> peer,
		MessageFlags &flags) {
	InnerFillMessagePostFlags(action.options, peer, flags);
}

namespace {

struct ConfirmedLocalFile {
	std::shared_ptr<FilePrepareResult> file;
	not_null<History*> history;
	not_null<PeerData*> peer;
	FullMsgId newId;
	SendAction action;
	TextWithEntities caption;
	MTPMessageMedia media;
	MessageFlags flags = MessageFlags();
	HistoryItem *itemToEdit = nullptr;
	int starsPaid = 0;
};

[[nodiscard]] TextWithEntities PrepareConfirmedFileCaption(
		not_null<History*> history,
		not_null<Main::Session*> session,
		const std::shared_ptr<FilePrepareResult> &file) {
	auto caption = TextWithEntities{
		file->caption.text,
		TextUtilities::ConvertTextTagsToEntities(file->caption.tags)
	};
	const auto prepareFlags = Ui::ItemTextOptions(
		history,
		session->user()).flags;
	TextUtilities::PrepareForSending(caption, prepareFlags);
	TextUtilities::Trim(caption);
	return caption;
}

[[nodiscard]] MessageFlags PrepareConfirmedFileFlags(
		not_null<Main::Session*> session,
		const SendAction &action,
		not_null<PeerData*> peer,
		const std::shared_ptr<FilePrepareResult> &file,
		const QString &caption,
		bool isEditing) {
	const auto welcomeTemplate = file->to.options.welcomeTemplate;
	const auto groupId = welcomeTemplate
		? uint64(0)
		: file->album
		? file->album->groupId
		: uint64(0);
	auto flags = isEditing ? MessageFlags() : NewMessageFlags(peer);
	if (welcomeTemplate) {
		flags &= ~MessageFlag::Outgoing;
		flags |= MessageFlag::FakeHistoryItem;
	}
	if (file->to.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	if (!isEditing
		&& !welcomeTemplate
		&& !groupId
		&& !file->to.options.scheduled
		&& !file->to.options.shortcutId
		&& session->ephemeralMessages().wouldSendMedia(
			peer,
			file->to.replyTo,
			caption)) {
		flags |= MessageFlag::Ephemeral;
	}
	FillMessagePostFlags(action, peer, flags);
	if (file->to.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;

		// Scheduled messages have no 'edited' badge.
		flags |= MessageFlag::HideEdited;
	}
	if (file->to.options.shortcutId) {
		flags |= MessageFlag::ShortcutMessage;

		// Shortcut messages have no 'edited' badge.
		flags |= MessageFlag::HideEdited;
	}
	const auto mediaTtlSeconds = (file->to.options.scheduled
		|| (file->type != SendMediaType::Photo
			&& file->type != SendMediaType::File))
		? crl::time()
		: file->to.options.ttlSeconds;
	if (file->type == SendMediaType::Audio
		|| file->type == SendMediaType::Round
		|| mediaTtlSeconds) {
		if (!peer->isChannel() || peer->isMegagroup()) {
			flags |= MessageFlag::MediaIsUnread;
		}
	}
	if (file->to.options.invertCaption) {
		flags |= MessageFlag::InvertMedia;
	}
	return flags;
}

[[nodiscard]] MTPMessageMedia PrepareConfirmedFileMedia(
		const std::shared_ptr<FilePrepareResult> &file) {
	const auto mediaTtlSeconds = (file->to.options.scheduled
		|| (file->type != SendMediaType::Photo
			&& file->type != SendMediaType::File))
		? crl::time()
		: file->to.options.ttlSeconds;
	return MTPMessageMedia([&] {
		if (file->type == SendMediaType::Photo) {
			using Flag = MTPDmessageMediaPhoto::Flag;
			return MTP_messageMediaPhoto(
				MTP_flags(Flag::f_photo
					| (file->spoiler ? Flag::f_spoiler : Flag())
					| (mediaTtlSeconds ? Flag::f_ttl_seconds : Flag())),
				file->photo,
				MTP_int(mediaTtlSeconds),
				MTPDocument()); // video
		} else if (file->type == SendMediaType::File) {
			using Flag = MTPDmessageMediaDocument::Flag;
			return MTP_messageMediaDocument(
				MTP_flags(Flag::f_document
					| (file->spoiler ? Flag::f_spoiler : Flag())
					| (file->videoCover ? Flag::f_video_cover : Flag())
					| (mediaTtlSeconds ? Flag::f_ttl_seconds : Flag())),
				file->document,
				MTPVector<MTPDocument>(), // alt_documents
				file->videoCover ? file->videoCover->photo : MTPPhoto(),
				MTPint(), // video_timestamp
				MTP_int(mediaTtlSeconds));
		} else if (file->type == SendMediaType::Audio) {
			const auto ttlSeconds = file->to.options.ttlSeconds;
			using Flag = MTPDmessageMediaDocument::Flag;
			return MTP_messageMediaDocument(
				MTP_flags(Flag::f_document
					| Flag::f_voice
					| (ttlSeconds ? Flag::f_ttl_seconds : Flag())
					| (file->videoCover ? Flag::f_video_cover : Flag())),
				file->document,
				MTPVector<MTPDocument>(), // alt_documents
				file->videoCover ? file->videoCover->photo : MTPPhoto(),
				MTPint(), // video_timestamp
				MTP_int(ttlSeconds));
		} else if (file->type == SendMediaType::Round) {
			using Flag = MTPDmessageMediaDocument::Flag;
			const auto ttlSeconds = file->to.options.ttlSeconds;
			return MTP_messageMediaDocument(
				MTP_flags(Flag::f_document
					| Flag::f_round
					| (ttlSeconds ? Flag::f_ttl_seconds : Flag())
					| (file->spoiler ? Flag::f_spoiler : Flag())),
				file->document,
				MTPVector<MTPDocument>(), // alt_documents
				MTPPhoto(), // video_cover
				MTPint(), // video_timestamp
				MTP_int(ttlSeconds));
		}
		Unexpected("Type in sendFilesConfirmed.");
	}());
}

[[nodiscard]] ConfirmedLocalFile PrepareConfirmedLocalFile(
		not_null<Main::Session*> session,
		const std::shared_ptr<FilePrepareResult> &file,
		int starsPaid) {
	const auto isEditing = (file->type != SendMediaType::Audio)
		&& (file->type != SendMediaType::Round)
		&& (file->to.replaceMediaOf != 0);
	const auto newId = FullMsgId(
		file->to.peer,
		(isEditing
			? file->to.replaceMediaOf
			: session->data().nextLocalMessageId()));
	const auto welcomeTemplate = file->to.options.welcomeTemplate;
	if (!welcomeTemplate && file->album) {
		const auto it = ranges::find(
			file->album->items,
			file->taskId,
			&SendingAlbum::Item::taskId);
		Assert(it != file->album->items.end());

		it->msgId = newId;
	}

	const auto itemToEdit = isEditing
		? session->data().message(newId)
		: nullptr;
	const auto history = session->data().history(file->to.peer);
	const auto peer = history->peer;

	if (!isEditing) {
		auto &histories = session->data().histories();
		file->to.replyTo.messageId = histories.convertTopicReplyToId(
			history,
			file->to.replyTo.messageId);
		file->to.replyTo.topicRootId = histories.convertTopicReplyToId(
			history,
			file->to.replyTo.topicRootId);
	}

	auto action = SendAction(history, file->to.options);
	action.clearDraft = false;
	action.replyTo = file->to.replyTo;
	action.generateLocal = true;
	action.replaceMediaOf = file->to.replaceMediaOf;

	auto caption = PrepareConfirmedFileCaption(history, session, file);
	const auto flags = PrepareConfirmedFileFlags(
		session,
		action,
		peer,
		file,
		caption.text,
		isEditing);

	return ConfirmedLocalFile{
		file,
		history,
		peer,
		newId,
		action,
		std::move(caption),
		PrepareConfirmedFileMedia(file),
		flags,
		itemToEdit,
		starsPaid,
	};
}

void AddConfirmedLocalPlaceholder(const ConfirmedLocalFile &local) {
	if (local.itemToEdit) {
		auto edition = HistoryMessageEdition();
		edition.isEditHide = (local.flags & MessageFlag::HideEdited);
		edition.editDate = 0;
		edition.ttl = 0;
		edition.mtpMedia = &local.media;
		edition.textWithEntities = local.caption;
		edition.invertMedia = local.file->to.options.invertCaption;
		edition.useSameViews = true;
		edition.useSameForwards = true;
		edition.useSameMarkup = true;
		edition.useSameReplies = true;
		edition.useSameReactions = true;
		edition.useSameSuggest = true;
		edition.savePreviousMedia = true;
		local.itemToEdit->applyEdition(std::move(edition));
		return;
	}

	const auto welcomeTemplate = local.file->to.options.welcomeTemplate;
	const auto item = local.history->addNewLocalMessage({
		.id = local.newId.msg,
		.flags = local.flags,
		.from = NewMessageFromId(local.action),
		.replyTo = local.file->to.replyTo,
		.date = NewMessageDate(local.file->to.options),
		.scheduleRepeatPeriod = local.file->to.options.scheduleRepeatPeriod,
		.shortcutId = local.file->to.options.shortcutId,
		.starsPaid = local.starsPaid,
		.postAuthor = NewMessagePostAuthor(local.action),
		.groupedId = welcomeTemplate
			? uint64(0)
			: local.file->album
			? local.file->album->groupId
			: uint64(0),
		.effectId = local.file->to.options.effectId,
		.suggest = HistoryMessageSuggestInfo(local.file->to.options),
	}, local.caption, local.media);
	if (welcomeTemplate) {
		local.history->session().welcomeMessages().appendSending(item);
	}
}

[[nodiscard]] bool FlushPreparedMusicBatch(
		not_null<Main::Session*> session,
		const std::shared_ptr<FilePrepareResult> &sample) {
	const auto album = sample->album;
	if (!album || !album->preparedMusicBatching()) {
		return false;
	}
	auto files = album->takePreparedMusic();
	if (files.empty()) {
		return false;
	}

	const auto peer = session->data().history(files.front()->to.peer)->peer;
	auto remainingStarsApproved = album->options.starsApproved;
	auto locals = std::vector<ConfirmedLocalFile>();
	locals.reserve(files.size());
	for (const auto &file : files) {
		const auto starsPaid = std::min(
			peer->starsPerMessageChecked(),
			remainingStarsApproved);
		remainingStarsApproved -= starsPaid;
		locals.push_back(PrepareConfirmedLocalFile(session, file, starsPaid));
	}

	auto notifyHistory = false;
	for (const auto &local : locals) {
		session->api().sendAction(local.action);
		AddConfirmedLocalPlaceholder(local);
		notifyHistory = notifyHistory
			|| (!local.itemToEdit
				&& !local.file->to.options.welcomeTemplate);
	}
	for (const auto &local : locals) {
		session->uploader().upload(local.newId, local.file);
	}

	if (notifyHistory) {
		session->data().sendHistoryChangeNotifications();
		session->changes().historyUpdated(
			locals.front().history,
			(locals.front().action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
	}
	return true;
}

} // namespace

void SendConfirmedFile(
		not_null<Main::Session*> session,
		const std::shared_ptr<FilePrepareResult> &file) {
	const auto welcomeTemplate = file->to.options.welcomeTemplate;
	if (welcomeTemplate && file->to.replaceMediaOf) {
		return;
	}
	if (welcomeTemplate) {
		const auto history = session->data().history(file->to.peer);
		if (session->welcomeMessages().count(history)
			>= Data::WelcomeMessagesLimit(session)) {
			return;
		}
	}
	if (FlushPreparedMusicBatch(session, file)) {
		return;
	}

	const auto history = session->data().history(file->to.peer);
	const auto local = PrepareConfirmedLocalFile(
		session,
		file,
		std::min(
			history->peer->starsPerMessageChecked(),
			file->to.options.starsApproved));
	session->uploader().upload(local.newId, file);
	session->api().sendAction(local.action);
	AddConfirmedLocalPlaceholder(local);

	if (local.itemToEdit) {
		return;
	}
	if (welcomeTemplate) {
		return;
	}

	session->data().sendHistoryChangeNotifications();
	session->changes().historyUpdated(
		local.history,
		(local.action.options.scheduled
			? Data::HistoryUpdate::Flag::ScheduledSent
			: Data::HistoryUpdate::Flag::MessageSent));
}

} // namespace Api
