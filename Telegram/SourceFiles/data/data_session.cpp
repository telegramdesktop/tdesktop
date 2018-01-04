/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "observer_peer.h"
#include "history/history_item_components.h"
#include "data/data_feed.h"

namespace Data {

Session::Session() {
	Notify::PeerUpdateViewer(
		Notify::PeerUpdate::Flag::UserIsContact
	) | rpl::map([](const Notify::PeerUpdate &update) {
		return update.peer->asUser();
	}) | rpl::filter([](UserData *user) {
		return user != nullptr;
	}) | rpl::start_with_next([=](not_null<UserData*> user) {
		userIsContactUpdated(user);
	}, _lifetime);
}

Session::~Session() = default;

void Session::markItemLayoutChanged(not_null<const HistoryItem*> item) {
	_itemLayoutChanged.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemLayoutChanged() const {
	return _itemLayoutChanged.events();
}

void Session::requestItemRepaint(not_null<const HistoryItem*> item) {
	_itemRepaintRequest.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRepaintRequest() const {
	return _itemRepaintRequest.events();
}

void Session::markItemRemoved(not_null<const HistoryItem*> item) {
	_itemRemoved.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRemoved() const {
	return _itemRemoved.events();
}

void Session::markHistoryUnloaded(not_null<const History*> history) {
	_historyUnloaded.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyUnloaded() const {
	return _historyUnloaded.events();
}

void Session::markHistoryCleared(not_null<const History*> history) {
	_historyCleared.fire_copy(history);
}

rpl::producer<not_null<const History*>> Session::historyCleared() const {
	return _historyCleared.events();
}

void Session::removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantRemoved.fire({ channel, user });
}

auto Session::megagroupParticipantRemoved() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantRemoved.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantRemoved(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	_megagroupParticipantAdded.fire({ channel, user });
}

auto Session::megagroupParticipantAdded() const
-> rpl::producer<MegagroupParticipant> {
	return _megagroupParticipantAdded.events();
}

rpl::producer<not_null<UserData*>> Session::megagroupParticipantAdded(
		not_null<ChannelData*> channel) const {
	return megagroupParticipantAdded(
	) | rpl::filter([channel](auto updateChannel, auto user) {
		return (updateChannel == channel);
	}) | rpl::map([](auto updateChannel, auto user) {
		return user;
	});
}

void Session::markStickersUpdated() {
	_stickersUpdated.fire({});
}

rpl::producer<> Session::stickersUpdated() const {
	return _stickersUpdated.events();
}

void Session::markSavedGifsUpdated() {
	_savedGifsUpdated.fire({});
}

rpl::producer<> Session::savedGifsUpdated() const {
	return _savedGifsUpdated.events();
}

void Session::userIsContactUpdated(not_null<UserData*> user) {
	const auto &items = App::sharedContactItems();
	const auto i = items.constFind(peerToUser(user->id));
	if (i != items.cend()) {
		for (const auto item : std::as_const(i.value())) {
			item->setPendingInitDimensions();
		}
	}
}

HistoryItemsList Session::idsToItems(
		const MessageIdsList &ids) const {
	return ranges::view::all(
		ids
	) | ranges::view::transform([](const FullMsgId &fullId) {
		return App::histItemById(fullId);
	}) | ranges::view::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::view::transform([](HistoryItem *item) {
		return not_null<HistoryItem*>(item);
	}) | ranges::to_vector;
}

MessageIdsList Session::itemsToIds(
		const HistoryItemsList &items) const {
	return ranges::view::all(
		items
	) | ranges::view::transform([](not_null<HistoryItem*> item) {
		return item->fullId();
	}) | ranges::to_vector;
}

MessageIdsList Session::groupToIds(
		not_null<HistoryMessageGroup*> group) const {
	auto result = itemsToIds(group->others);
	result.push_back(group->leader->fullId());
	return result;
}

not_null<Data::Feed*> Session::feed(FeedId id) {
	if (const auto result = feedLoaded(id)) {
		return result;
	}
	const auto [it, ok] = _feeds.emplace(
		id,
		std::make_unique<Data::Feed>(id));
	return it->second.get();
}

Data::Feed *Session::feedLoaded(FeedId id) {
	const auto it = _feeds.find(id);
	return (it == _feeds.end()) ? nullptr : it->second.get();
}

} // namespace Data
