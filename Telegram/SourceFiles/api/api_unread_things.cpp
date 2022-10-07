/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_unread_things.h"

#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "apiwrap.h"

namespace Api {
namespace {

constexpr auto kPreloadIfLess = 5;
constexpr auto kFirstRequestLimit = 10;
constexpr auto kNextRequestLimit = 100;


[[nodiscard]] not_null<History*> ResolveHistory(
		not_null<Dialogs::Entry*> entry) {
	if (const auto history = entry->asHistory()) {
		return history;
	}
	const auto topic = entry->asTopic();

	Ensures(topic != nullptr);
	return topic->history();
}

} // namespace

UnreadThings::UnreadThings(not_null<ApiWrap*> api) : _api(api) {
}

bool UnreadThings::trackMentions(PeerData *peer) const {
	return peer && (peer->isChat() || peer->isMegagroup());
}

bool UnreadThings::trackReactions(PeerData *peer) const {
	return trackMentions(peer) || (peer && peer->isUser());
}

void UnreadThings::preloadEnough(DialogsEntry *entry) {
	if (!entry) {
		return;
	}
	const auto history = ResolveHistory(entry);
	if (trackMentions(history->peer)) {
		preloadEnoughMentions(history);
	}
	if (trackReactions(history->peer)) {
		preloadEnoughReactions(history);
	}
}

void UnreadThings::mediaAndMentionsRead(
		const base::flat_set<MsgId> &readIds,
		ChannelData *channel) {
	for (const auto &msgId : readIds) {
		_api->requestMessageData(channel, msgId, [=] {
			const auto item = channel
				? _api->session().data().message(channel->id, msgId)
				: _api->session().data().nonChannelMessage(msgId);
			if (item && item->mentionsMe()) {
				item->markMediaAndMentionRead();
			}
		});
	}
}

void UnreadThings::preloadEnoughMentions(not_null<DialogsEntry*> entry) {
	const auto fullCount = entry->unreadMentions().count();
	const auto loadedCount = entry->unreadMentions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestMentions(entry, loadedCount);
	}
}

void UnreadThings::preloadEnoughReactions(not_null<DialogsEntry*> entry) {
	const auto fullCount = entry->unreadReactions().count();
	const auto loadedCount = entry->unreadReactions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestReactions(entry, loadedCount);
	}
}

void UnreadThings::cancelRequests(not_null<DialogsEntry*> entry) {
	if (const auto requestId = _mentionsRequests.take(entry)) {
		_api->request(*requestId).cancel();
	}
	if (const auto requestId = _reactionsRequests.take(entry)) {
		_api->request(*requestId).cancel();
	}
}

void UnreadThings::requestMentions(
		not_null<DialogsEntry*> entry,
		int loaded) {
	if (_mentionsRequests.contains(entry)) {
		return;
	}
	const auto offsetId = std::max(
		entry->unreadMentions().maxLoaded(),
		MsgId(1));
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto history = ResolveHistory(entry);
	const auto topic = entry->asTopic();
	using Flag = MTPmessages_GetUnreadMentions::Flag;
	const auto requestId = _api->request(MTPmessages_GetUnreadMentions(
		MTP_flags(topic ? Flag::f_top_msg_id : Flag()),
		history->peer->input,
		MTP_int(topic ? topic->rootId() : 0),
		MTP_int(offsetId),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId)
	)).done([=](const MTPmessages_Messages &result) {
		_mentionsRequests.remove(history);
		history->unreadMentions().addSlice(result, loaded);
	}).fail([=] {
		_mentionsRequests.remove(history);
	}).send();
	_mentionsRequests.emplace(history, requestId);
}

void UnreadThings::requestReactions(
		not_null<DialogsEntry*> entry,
		int loaded) {
	if (_reactionsRequests.contains(entry)) {
		return;
	}
	const auto offsetId = loaded
		? std::max(entry->unreadReactions().maxLoaded(), MsgId(1))
		: MsgId(1);
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto history = ResolveHistory(entry);
	const auto topic = entry->asTopic();
	using Flag = MTPmessages_GetUnreadReactions::Flag;
	const auto requestId = _api->request(MTPmessages_GetUnreadReactions(
		MTP_flags(topic ? Flag::f_top_msg_id : Flag()),
		history->peer->input,
		MTP_int(topic ? topic->rootId() : 0),
		MTP_int(offsetId),
		MTP_int(addOffset),
		MTP_int(limit),
		MTP_int(maxId),
		MTP_int(minId)
	)).done([=](const MTPmessages_Messages &result) {
		_reactionsRequests.remove(history);
		history->unreadReactions().addSlice(result, loaded);
	}).fail([=] {
		_reactionsRequests.remove(history);
	}).send();
	_reactionsRequests.emplace(history, requestId);
}

} // namespace UnreadThings
