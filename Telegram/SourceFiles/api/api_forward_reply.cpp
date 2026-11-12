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
	const auto sent = [=](const MessageIdsList &ids) {
		const auto id = (ids.size() == 1) ? ids.front() : FullMsgId();
		if (text.text.trimmed().isEmpty()) {
			if (id && forwarded) {
				forwarded(id);
			}
			return;
		}
		auto message = MessageToSend(reply);
		message.action.replyTo = FullReplyTo{
			.messageId = id,
			.topicRootId = topicRootId,
			.monoforumPeerId = monoforumPeerId,
		};
		message.textWithTags = text;
		message.webPage = webPage;
		session->api().sendMessage(std::move(message));
	};
	session->api().sendAction(descriptor.action);
	session->api().forwardMessages(
		std::move(draft),
		descriptor.action,
		nullptr,
		sent);
	session->changes().historyUpdated(
		history,
		Data::HistoryUpdate::Flag::MessageSent);
	return true;
}

} // namespace Api
