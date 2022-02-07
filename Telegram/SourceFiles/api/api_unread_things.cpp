/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_unread_things.h"

#include "data/data_peer.h"
#include "data/data_channel.h"
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

bool UnreadThings::trackMentions(PeerData *peer) const {
	return peer && (peer->isChat() || peer->isMegagroup());
}

bool UnreadThings::trackReactions(PeerData *peer) const {
	return trackMentions(peer) || (peer && peer->isUser());
}

void UnreadThings::preloadEnough(History *history) {
	if (!history) {
		return;
	}
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

void UnreadThings::preloadEnoughMentions(not_null<History*> history) {
	const auto fullCount = history->unreadMentions().count();
	const auto loadedCount = history->unreadMentions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestMentions(history, loadedCount);
	}
}

void UnreadThings::preloadEnoughReactions(not_null<History*> history) {
	const auto fullCount = history->unreadReactions().count();
	const auto loadedCount = history->unreadReactions().loadedCount();
	const auto allLoaded = (fullCount >= 0) && (loadedCount >= fullCount);
	if (fullCount >= 0 && loadedCount < kPreloadIfLess && !allLoaded) {
		requestReactions(history, loadedCount);
	}
}

void UnreadThings::requestMentions(not_null<History*> history, int loaded) {
	if (_mentionsRequests.contains(history)) {
		return;
	}
	const auto offsetId = std::max(
		history->unreadMentions().maxLoaded(),
		MsgId(1));
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto requestId = _api->request(MTPmessages_GetUnreadMentions(
		history->peer->input,
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

void UnreadThings::requestReactions(not_null<History*> history, int loaded) {
	if (_reactionsRequests.contains(history)) {
		return;
	}
	const auto offsetId = loaded
		? std::max(history->unreadReactions().maxLoaded(), MsgId(1))
		: MsgId(1);
	const auto limit = loaded ? kNextRequestLimit : kFirstRequestLimit;
	const auto addOffset = loaded ? -(limit + 1) : -limit;
	const auto maxId = 0;
	const auto minId = 0;
	const auto requestId = _api->request(MTPmessages_GetUnreadReactions(
		history->peer->input,
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
