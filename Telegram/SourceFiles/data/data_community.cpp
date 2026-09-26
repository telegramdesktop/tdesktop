/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_community.h"

#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "styles/style_dialogs.h"

namespace Data {
namespace {

constexpr auto kShowChatNamesCount = 20;

[[nodiscard]] TextWithEntities ComposeCommunityListEntryText(
		not_null<CommunityInfo*> info) {
	const auto &list = info->lastHistories();
	if (list.empty()) {
		return {};
	}

	auto notInCount = 0;
	for (const auto &linked : info->linkedPeers()) {
		if (!CommunityChatJoined(linked.peer)) {
			++notInCount;
		}
	}
	auto &&peers = ranges::views::all(list);
	const auto wrapName = [](not_null<History*> history) {
		const auto name = history->peer->name();
		return st::wrap_rtl(TextWithEntities{
			.text = name,
			.entities = (history->chatListBadgesState().unread
				? EntitiesInText{
					{
						EntityType::Semibold,
						0,
						int(name.size()),
						QString(),
					},
					{
						EntityType::Colorized,
						0,
						int(name.size()),
						QString(),
					},
				}
				: EntitiesInText{}),
		});
	};
	const auto shown = int(peers.size());
	const auto accumulated = [&] {
		Expects(shown > 0);

		auto i = peers.begin();
		auto result = wrapName(*i);
		for (++i; i != peers.end(); ++i) {
			result = tr::lng_archived_last_list(
				tr::now,
				lt_accumulated,
				result,
				lt_chat,
				wrapName(*i),
				tr::marked);
		}
		return result;
	}();
	return notInCount
		? tr::lng_archived_last(
			tr::now,
			lt_count,
			notInCount,
			lt_chats,
			accumulated,
			tr::marked)
		: accumulated;
}

} // namespace

bool IsCommunityChatViewable(const CommunityLinkedPeer &linked) {
	return linked.canViewHistory;
}

ChannelId PeerLinkedCommunityId(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return channel->linkedCommunityId();
	} else if (const auto user = peer->asUser()) {
		return user->linkedCommunityId();
	}
	return ChannelId();
}

bool CommunityChatJoined(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return channel->amIn();
	} else if (peer->isUser()) {
		const auto history = peer->owner().historyLoaded(peer);
		return history && (history->chatListMessage() != nullptr);
	}
	return false;
}

bool CommunityChatJoined(not_null<const History*> history) {
	if (const auto channel = history->peer->asChannel()) {
		return channel->amIn();
	} else if (history->peer->isUser()) {
		return history->chatListMessage() != nullptr;
	}
	return false;
}

CommunityInfo::CommunityInfo(not_null<ChannelData*> channel)
: _channel(channel)
, _chatsList(
	&channel->session(),
	FilterId(),
	channel->owner().maxPinnedChatsLimitValue(
		static_cast<Data::Folder*>(nullptr)))
, _collapsedInChatLists(channel->collapsedInDialogs()) {
	_channel->session().changes().peerUpdates(
		PeerUpdate::Flag::Name
	) | rpl::filter([=](const PeerUpdate &update) {
		return ranges::contains(_lastHistories, update.peer, &History::peer);
	}) | rpl::on_next([=] {
		++_chatListViewVersion;
		repaintRow();
	}, _lifetime);
}

not_null<Dialogs::MainList*> CommunityInfo::chatsList() {
	return &_chatsList;
}

void CommunityInfo::applyLinkedPeers(const QVector<MTPCommunityPeer> &list) {
	auto now = std::vector<CommunityLinkedPeer>();
	now.reserve(list.size());
	auto &owner = _channel->owner();
	for (const auto &entry : list) {
		const auto &data = entry.data();
		auto visible = std::optional<bool>();
		if (const auto value = data.vvisible()) {
			visible = mtpIsTrue(*value);
		}
		now.push_back({
			.peer = owner.peer(peerFromMTP(data.vpeer())),
			.visible = visible,
			.canViewHistory = data.is_can_view_history(),
		});
	}
	if (_linkedPeers == now) {
		return;
	}
	_linkedPeers = std::move(now);

	_hiddenPeers.clear();
	for (const auto &linked : _linkedPeers) {
		if (linked.visible.has_value() && !*linked.visible) {
			_hiddenPeers.emplace(linked.peer);
		}
	}

	const auto communityId = peerToChannel(_channel->id);
	for (const auto &linked : _linkedPeers) {
		if (const auto channel = linked.peer->asChannel()) {
			channel->setLinkedCommunityId(communityId);
		} else if (const auto user = linked.peer->asUser()) {
			user->setLinkedCommunityId(communityId);
		}
	}
	const auto stillLinked = [&](not_null<History*> history) {
		return ranges::contains(
			_linkedPeers,
			history->peer,
			&CommunityLinkedPeer::peer);
	};
	const auto unlinkStale = [&](
			const base::flat_set<not_null<History*>> &set) {
		for (const auto &history : base::duplicate(set)) {
			if (!stillLinked(history)) {
				const auto channel = history->peer->asChannel();
				const auto user = history->peer->asUser();
				if (channel
					&& channel->linkedCommunityId() == communityId) {
					channel->setLinkedCommunityId(ChannelId());
				} else if (user
					&& user->linkedCommunityId() == communityId) {
					user->setLinkedCommunityId(ChannelId());
				}
			}
		}
	};
	unlinkStale(_histories);
	unlinkStale(_otherHistories);
	++_chatListViewVersion;
	repaintRow();
	_linkedPeersChanges.fire({});
}

rpl::producer<> CommunityInfo::linkedPeersValue() const {
	return rpl::single(
		rpl::empty
	) | rpl::then(_linkedPeersChanges.events());
}

rpl::producer<> CommunityInfo::refreshed() const {
	return _refreshed.events();
}

bool CommunityInfo::isHidden(not_null<PeerData*> peer) const {
	return _hiddenPeers.contains(peer);
}

bool CommunityInfo::collapsedInDialogs() const {
	return _channel->collapsedInDialogs();
}

void CommunityInfo::collapsedChanged() {
	// The channel flag is already flipped when this runs, while rows and
	// pinned state of the linked histories still live in the old lists.
	// _collapsedInChatLists feeds Data::Session::chatsListFor(), so it
	// must keep naming the old list until every linked history is removed
	// from it (the unpin inside removeFromChatList and the pinned-index
	// reindex of not-yet-moved siblings resolve lists through it), and
	// flip exactly once before the histories are re-added to the new one.
	const auto nowCollapsed = collapsedInDialogs();
	auto &owner = _channel->owner();
	auto moved = base::flat_set<not_null<History*>>();
	const auto removeAll = [&](
			const base::flat_set<not_null<History*>> &from) {
		for (const auto &history : from) {
			if (history->inChatList()) {
				const auto wasList = _collapsedInChatLists
					? chatsList()
					: owner.chatsList(history->folder());
				history->removeFromChatList(0, wasList);
				moved.emplace(history);
			}
		}
	};
	removeAll(_histories);
	removeAll(_otherHistories);
	_collapsedInChatLists = nowCollapsed;
	for (const auto &history : moved) {
		history->updateChatListSortPosition();
	}
	for (const auto &history : _histories) {
		if (!moved.contains(history)) {
			history->updateChatListSortPosition();
			history->updateChatListExistence();
		}
	}
	ensureRowInChatList();
	repaintRow();
}

void CommunityInfo::ensureRowInChatList() {
	const auto history = _channel->owner().history(_channel);
	if (!history->folderKnown()) {
		history->clearFolder();
	}
	history->requestChatListMessage();
	history->updateChatListSortPosition();
	history->updateChatListExistence();
}

void CommunityInfo::registerOne(not_null<History*> history) {
	if (CommunityChatJoined(history)) {
		if (!_histories.emplace(history).second) {
			return;
		}
		memberAdded(history);
	} else if (!_otherHistories.emplace(history).second) {
		return;
	}
	ensureRowInChatList();
	updateRowSortPosition();
}

void CommunityInfo::unregisterOne(not_null<History*> history) {
	if (!_histories.remove(history)) {
		_otherHistories.remove(history);
		return;
	}
	if (history->chatListTimeId() >= _chatsListDate) {
		recountChatsListDate();
	}
	reorderLastHistories();
	updateRowSortPosition();
}

void CommunityInfo::refreshOneMembership(not_null<History*> history) {
	if (CommunityChatJoined(history)) {
		if (_histories.contains(history)
			|| !_otherHistories.remove(history)) {
			return;
		}
		_histories.emplace(history);
		memberAdded(history);
		updateRowSortPosition();
		_linkedPeersChanges.fire({});
	} else {
		if (!_histories.remove(history)) {
			return;
		}
		_otherHistories.emplace(history);
		if (history->chatListTimeId() >= _chatsListDate) {
			recountChatsListDate();
		}
		reorderLastHistories();
		updateRowSortPosition();
		_linkedPeersChanges.fire({});
	}
}

void CommunityInfo::memberAdded(not_null<History*> history) {
	const auto date = history->chatListTimeId();
	if (date > _chatsListDate) {
		_chatsListDate = date;
	}
	reorderLastHistories();
	if (collapsedInDialogs()) {
		history->updateChatListExistence();
	}
}

void CommunityInfo::oneChatsListDateChanged(TimeId was, TimeId now) {
	if (now >= _chatsListDate) {
		_chatsListDate = now;
		updateRowSortPosition();
	} else if (was >= _chatsListDate) {
		recountChatsListDate();
		updateRowSortPosition();
	}
}

void CommunityInfo::oneListMessageChanged() {
	reorderLastHistories();
}

void CommunityInfo::oneUnreadStateChanged() {
	++_chatListViewVersion;
	repaintRow();
}

void CommunityInfo::recountChatsListDate() {
	auto result = TimeId(0);
	for (const auto &history : _histories) {
		result = std::max(result, history->chatListTimeId());
	}
	_chatsListDate = result;
}

void CommunityInfo::reorderLastHistories() {
	const auto pred = [](not_null<History*> a, not_null<History*> b) {
		const auto aItem = a->chatListMessage();
		const auto bItem = b->chatListMessage();
		const auto aDate = aItem ? aItem->date() : TimeId(0);
		const auto bDate = bItem ? bItem->date() : TimeId(0);
		return aDate > bDate;
	};
	_lastHistories.clear();
	_lastHistories.reserve(
		std::min(int(_histories.size()), kShowChatNamesCount));
	for (const auto &history : _histories) {
		const auto i = ranges::upper_bound(_lastHistories, history, pred);
		if (int(_lastHistories.size()) < kShowChatNamesCount
			|| i != end(_lastHistories)) {
			_lastHistories.insert(i, history);
		}
		if (int(_lastHistories.size()) > kShowChatNamesCount) {
			_lastHistories.pop_back();
		}
	}
	++_chatListViewVersion;
	repaintRow();
}

void CommunityInfo::updateRowSortPosition() {
	if (const auto history = _channel->owner().historyLoaded(_channel)) {
		history->updateChatListSortPosition();
	}
}

void CommunityInfo::repaintRow() {
	if (const auto history = _channel->owner().historyLoaded(_channel)) {
		history->updateChatListEntry();
	}
	_refreshed.fire({});
}

void CommunityInfo::validateListEntryCache() {
	if (_listEntryCacheVersion == _chatListViewVersion) {
		return;
	}
	_listEntryCacheVersion = _chatListViewVersion;
	_listEntryCache.setMarkedText(
		st::dialogsTextStyle,
		ComposeCommunityListEntryText(this),
		// Use rich options as long as the entry text does not have user text.
		Ui::ItemTextDefaultOptions());
}

} // namespace Data
