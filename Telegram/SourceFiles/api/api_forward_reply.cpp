/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_forward_reply.h"

#include "apiwrap.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "main/main_session.h"

namespace Api {

bool CanReplyToForward(const HistoryItemsList &items) {
	return (items.size() == 1) && !items.front()->isSavedMusicItem();
}

bool SendForwardWithReply(ForwardReply &&descriptor) {
	const auto history = descriptor.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto topicRootId = descriptor.action.replyTo.topicRootId;
	const auto monoforumPeerId = descriptor.action.replyTo.monoforumPeerId;
	auto draft = history->resolveForwardDraft(topicRootId, monoforumPeerId);
	if (!CanReplyToForward(draft.items)) {
		return false;
	}
	const auto text = std::move(descriptor.text);
	auto request = SendingErrorRequest{
		.topicRootId = topicRootId,
		.forward = &draft.items,
		.text = &text,
	};
	request.messagesCount = ComputeSendingMessagesCount(history, request);
	if (const auto error = GetErrorForSending(peer, request)) {
		Data::ShowSendErrorToast(descriptor.show, peer, error);
		return false;
	} else if (!descriptor.checkPayment(request.messagesCount)) {
		return false;
	}
	history->setForwardDraft(topicRootId, monoforumPeerId, {});

	auto reply = descriptor.action;
	reply.options.starsApproved -= std::min(
		reply.options.starsApproved,
		peer->starsPerMessageChecked());
	const auto webPage = std::move(descriptor.webPage);
	const auto forwarded = std::move(descriptor.forwarded);
	const auto hasText = !text.text.trimmed().isEmpty();
	const auto sendText = [=](FullMsgId replyToId) {
		auto message = MessageToSend(reply);
		message.action.replyTo = FullReplyTo{
			.messageId = replyToId,
			.topicRootId = topicRootId,
			.monoforumPeerId = monoforumPeerId,
		};
		message.textWithTags = text;
		message.webPage = webPage;
		const auto localId = session->data().nextLocalMessageId();
		session->api().sendMessage(std::move(message), localId);
		return FullMsgId(peer->id, localId);
	};
	const auto waiting = std::make_shared<FullMsgId>();
	const auto sent = [=](const MessageIdsList &ids) {
		const auto id = (ids.size() == 1) ? ids.front() : FullMsgId();
		if (!hasText) {
			if (id && forwarded) {
				forwarded(id);
			}
		} else if (!*waiting || (!id && session->data().message(*waiting))) {
			sendText(id);
		}
	};
	session->api().sendAction(descriptor.action);
	const auto localId = session->api().forwardMessages(
		std::move(draft),
		descriptor.action,
		nullptr,
		sent);
	if (localId && hasText) {
		*waiting = sendText(localId);
	}
	session->changes().historyUpdated(
		history,
		Data::HistoryUpdate::Flag::MessageSent);
	return true;
}

} // namespace Api
