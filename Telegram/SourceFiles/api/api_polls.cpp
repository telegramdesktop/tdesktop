/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_polls.h"

#include "api/api_common.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h" // ShouldSendSilent
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] TimeId UnixtimeFromMsgId(mtpMsgId msgId) {
	return TimeId(msgId >> 32);
}

} // namespace

Polls::Polls(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void Polls::create(
		const PollData &data,
		const SendAction &action,
		Fn<void()> done,
		Fn<void()> fail) {
	_session->api().sendAction(action);

	const auto history = action.history;
	const auto peer = history->peer;
	const auto topicRootId = action.replyTo.messageId
		? action.replyTo.topicRootId
		: 0;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	const auto clearCloudDraft = action.clearDraft;
	if (clearCloudDraft) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_clear_draft;
		history->clearLocalDraft(topicRootId);
		history->clearCloudDraft(topicRootId);
		history->startSavingCloudDraft(topicRootId);
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	if (action.options.scheduled) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	auto &histories = history->owner().histories();
	const auto randomId = base::RandomValue<uint64>();
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(sendFlags),
			peer->input,
			Data::Histories::ReplyToPlaceholder(),
			PollDataToInputMedia(&data),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			(sendAs ? sendAs->input : MTP_inputPeerEmpty())
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		_session->changes().historyUpdated(
			history,
			(action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
		done();
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		fail();
	});
}

void Polls::sendVotes(
		FullMsgId itemId,
		const std::vector<QByteArray> &options) {
	if (_pollVotesRequestIds.contains(itemId)) {
		return;
	}
	const auto item = _session->data().message(itemId);
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!item) {
		return;
	}

	const auto showSending = poll && !options.empty();
	const auto hideSending = [=] {
		if (showSending) {
			if (const auto item = _session->data().message(itemId)) {
				poll->sendingVotes.clear();
				_session->data().requestItemRepaint(item);
			}
		}
	};
	if (showSending) {
		poll->sendingVotes = options;
		_session->data().requestItemRepaint(item);
	}

	auto prepared = QVector<MTPbytes>();
	prepared.reserve(options.size());
	ranges::transform(
		options,
		ranges::back_inserter(prepared),
		[](const QByteArray &option) { return MTP_bytes(option); });
	const auto requestId = _api.request(MTPmessages_SendVote(
		item->history()->peer->input,
		MTP_int(item->id),
		MTP_vector<MTPbytes>(prepared)
	)).done([=](const MTPUpdates &result) {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollVotesRequestIds.erase(itemId);
		hideSending();
	}).send();
	_pollVotesRequestIds.emplace(itemId, requestId);
}

void Polls::close(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (_pollCloseRequestIds.contains(itemId)) {
		return;
	}
	const auto media = item ? item->media() : nullptr;
	const auto poll = media ? media->poll() : nullptr;
	if (!poll) {
		return;
	}
	const auto requestId = _api.request(MTPmessages_EditMessage(
		MTP_flags(MTPmessages_EditMessage::Flag::f_media),
		item->history()->peer->input,
		MTP_int(item->id),
		MTPstring(),
		PollDataToInputMedia(poll, true),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(0) // schedule_date
	)).done([=](const MTPUpdates &result) {
		_pollCloseRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollCloseRequestIds.erase(itemId);
	}).send();
	_pollCloseRequestIds.emplace(itemId, requestId);
}

void Polls::reloadResults(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	if (!item->isRegular() || _pollReloadRequestIds.contains(itemId)) {
		return;
	}
	const auto requestId = _api.request(MTPmessages_GetPollResults(
		item->history()->peer->input,
		MTP_int(item->id)
	)).done([=](const MTPUpdates &result) {
		_pollReloadRequestIds.erase(itemId);
		_session->updates().applyUpdates(result);
	}).fail([=] {
		_pollReloadRequestIds.erase(itemId);
	}).send();
	_pollReloadRequestIds.emplace(itemId, requestId);
}

} // namespace Api
