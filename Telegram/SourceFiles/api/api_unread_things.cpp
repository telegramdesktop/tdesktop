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

} // namespace

UnreadThings::UnreadThings(not_null<ApiWrap*> api) : _api(api) {
}

bool UnreadThings::trackMentions(Data::Thread *thread) const {
	const auto peer = thread ? thread->peer().get() : nullptr;
	return peer && (peer->isChat() || peer->isMegagroup());
}

bool UnreadThings::trackReactions(Data::Thread *thread) const {
	const auto peer = thread ? thread->peer().get() : nullptr;
	return peer && (peer->isUser() || peer->isChat() || peer->isMegagroup());
}

void UnreadThings::preloadEnough(Data::Thread *thread) {
	if (trackMentions(thread)) {
		preloadEnoughMentions(thread);
	}
	if (trackReactions(thread)) {
		preloadEnoughReactions(thread);
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

void UnreadThings::preloadEnoughMentions(not_null<Data::Thread*> thread) {
	const auto fullCount = thread->unreadMentions().count();
	const auto loadedCount = thread->unreadMentions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestMentions(thread, loadedCount);
	}
}

void UnreadThings::preloadEnoughReactions(not_null<Data::Thread*> thread) {
	const auto fullCount = thread->unreadReactions().count();
	const auto loadedCount = thread->unreadReactions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestReactions(thread, loadedCount);
	}
}

void UnreadThings::cancelRequests(not_null<Data::Thread*> thread) {
	if (const auto requestId = _mentionsRequests.take(thread)) {
		_api->request(*requestId).cancel();
	}
	if (const auto requestId = _reactionsRequests.take(thread)) {
		_api->request(*requestId).cancel();
	}
}

void UnreadThings::requestMentions(
		not_null<Data::Thread*> thread,
		int loaded) {
	if (_mentionsRequests.contains(thread)) {
		return;
	}
	const auto offsetId = std::max(
		thread->unreadMentions().maxLoaded(),
		MsgId(1));
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto history = thread->owningHistory();
	const auto topic = thread->asTopic();
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
		_mentionsRequests.remove(thread);
		thread->unreadMentions().addSlice(result, loaded);
	}).fail([=] {
		_mentionsRequests.remove(thread);
	}).send();
	_mentionsRequests.emplace(thread, requestId);
}

void UnreadThings::requestReactions(
		not_null<Data::Thread*> thread,
		int loaded) {
	if (_reactionsRequests.contains(thread)) {
		return;
	}
	const auto offsetId = loaded
		? std::max(thread->unreadReactions().maxLoaded(), MsgId(1))
		: MsgId(1);
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto history = thread->owningHistory();
	const auto topic = thread->asTopic();
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
		_reactionsRequests.remove(thread);
		thread->unreadReactions().addSlice(result, loaded);
	}).fail([=] {
		_reactionsRequests.remove(thread);
	}).send();
	_reactionsRequests.emplace(thread, requestId);
}

} // namespace UnreadThings
