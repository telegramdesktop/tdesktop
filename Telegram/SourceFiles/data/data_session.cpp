/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_session.h"

#include "observer_peer.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "data/data_feed.h"

namespace Data {

using ViewElement = HistoryView::Element;

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

void Session::registerItemView(not_null<ViewElement*> view) {
	_views[view->data()].push_back(view);
}

void Session::unregisterItemView(not_null<ViewElement*> view) {
	const auto i = _views.find(view->data());
	if (i != _views.end()) {
		auto &list = i->second;
		list.erase(ranges::remove(list, view), end(list));
		if (list.empty()) {
			_views.erase(i);
		}
	}
	if (App::hoveredItem() == view) {
		App::hoveredItem(nullptr);
	}
	if (App::pressedItem() == view) {
		App::pressedItem(nullptr);
	}
	if (App::hoveredLinkItem() == view) {
		App::hoveredLinkItem(nullptr);
	}
	if (App::pressedLinkItem() == view) {
		App::pressedLinkItem(nullptr);
	}
	if (App::mousedItem() == view) {
		App::mousedItem(nullptr);
	}
}

void Session::markItemLayoutChange(not_null<const HistoryItem*> item) {
	_itemLayoutChanges.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemLayoutChanged() const {
	return _itemLayoutChanges.events();
}

void Session::markViewLayoutChange(not_null<const HistoryView::Element*> view) {
	_viewLayoutChanges.fire_copy(view);
}

rpl::producer<not_null<const HistoryView::Element*>> Session::viewLayoutChanged() const {
	return _viewLayoutChanges.events();
}

void Session::markItemIdChange(IdChange event) {
	_itemIdChanges.fire_copy(event);
	enumerateItemViews(event.item, [](not_null<HistoryView::Element*> view) {
		view->refreshDataId();
	});
}

rpl::producer<Session::IdChange> Session::itemIdChanged() const {
	return _itemIdChanges.events();
}

void Session::requestItemRepaint(not_null<const HistoryItem*> item) {
	_itemRepaintRequest.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemRepaintRequest() const {
	return _itemRepaintRequest.events();
}

void Session::requestViewRepaint(not_null<const ViewElement*> view) {
	_viewRepaintRequest.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewRepaintRequest() const {
	return _viewRepaintRequest.events();
}

void Session::requestItemViewResize(not_null<const HistoryItem*> item) {
	_itemViewResizeRequest.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemViewResizeRequest() const {
	return _itemViewResizeRequest.events();
}

void Session::requestViewResize(not_null<const ViewElement*> view) {
	_viewResizeRequest.fire_copy(view);
}

rpl::producer<not_null<const ViewElement*>> Session::viewResizeRequest() const {
	return _viewResizeRequest.events();
}

void Session::requestItemViewRefresh(not_null<const HistoryItem*> item) {
	_itemViewRefreshRequest.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemViewRefreshRequest() const {
	return _itemViewRefreshRequest.events();
}

void Session::requestItemPlayInline(not_null<const HistoryItem*> item) {
	_itemPlayInlineRequest.fire_copy(item);
}

rpl::producer<not_null<const HistoryItem*>> Session::itemPlayInlineRequest() const {
	return _itemPlayInlineRequest.events();
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
			requestItemViewResize(item);
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

MessageIdsList Session::itemOrItsGroup(not_null<HistoryItem*> item) const {
	if (const auto group = groups().find(item)) {
		return itemsToIds(group->items);
	}
	return { 1, item->fullId() };
}

void Session::setPinnedDialog(const Dialogs::Key &key, bool pinned) {
	setIsPinned(key, pinned);
}

void Session::applyPinnedDialogs(const QVector<MTPDialog> &list) {
	clearPinnedDialogs();
	for (auto i = list.size(); i != 0;) {
		const auto &dialog = list[--i];
		switch (dialog.type()) {
		case mtpc_dialog: {
			const auto &dialogData = dialog.c_dialog();
			if (const auto peer = peerFromMTP(dialogData.vpeer)) {
				setPinnedDialog(App::history(peer), true);
			}
		} break;

		case mtpc_dialogFeed: {
			const auto &feedData = dialog.c_dialogFeed();
			const auto feedId = feedData.vfeed_id.v;
			setPinnedDialog(feed(feedId), true);
		} break;

		default: Unexpected("Type in ApiWrap::applyDialogsPinned.");
		}
	}
}

void Session::applyPinnedDialogs(const QVector<MTPDialogPeer> &list) {
	clearPinnedDialogs();
	for (auto i = list.size(); i != 0;) {
		const auto &dialogPeer = list[--i];
		switch (dialogPeer.type()) {
		case mtpc_dialogPeer: {
			const auto &peerData = dialogPeer.c_dialogPeer();
			if (const auto peerId = peerFromMTP(peerData.vpeer)) {
				setPinnedDialog(App::history(peerId), true);
			}
		} break;
		case mtpc_dialogPeerFeed: {
			const auto &feedData = dialogPeer.c_dialogPeerFeed();
			const auto feedId = feedData.vfeed_id.v;
			setPinnedDialog(feed(feedId), true);
		} break;
		}
	}
}

int Session::pinnedDialogsCount() const {
	return _pinnedDialogs.size();
}

const std::deque<Dialogs::Key> &Session::pinnedDialogsOrder() const {
	return _pinnedDialogs;
}

void Session::clearPinnedDialogs() {
	while (!_pinnedDialogs.empty()) {
		setPinnedDialog(_pinnedDialogs.back(), false);
	}
}

void Session::reorderTwoPinnedDialogs(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2) {
	const auto &order = pinnedDialogsOrder();
	const auto index1 = ranges::find(order, key1) - begin(order);
	const auto index2 = ranges::find(order, key2) - begin(order);
	Assert(index1 >= 0 && index1 < order.size());
	Assert(index2 >= 0 && index2 < order.size());
	Assert(index1 != index2);
	std::swap(_pinnedDialogs[index1], _pinnedDialogs[index2]);
	key1.entry()->cachePinnedIndex(index2 + 1);
	key2.entry()->cachePinnedIndex(index1 + 1);
}

void Session::setIsPinned(const Dialogs::Key &key, bool pinned) {
	const auto already = ranges::find(_pinnedDialogs, key);
	if (pinned) {
		if (already != end(_pinnedDialogs)) {
			auto saved = std::move(*already);
			const auto alreadyIndex = already - end(_pinnedDialogs);
			const auto count = int(size(_pinnedDialogs));
			Assert(alreadyIndex < count);
			for (auto index = alreadyIndex + 1; index != count; ++index) {
				_pinnedDialogs[index - 1] = std::move(_pinnedDialogs[index]);
				_pinnedDialogs[index - 1].entry()->cachePinnedIndex(index);
			}
			_pinnedDialogs.back() = std::move(saved);
			_pinnedDialogs.back().entry()->cachePinnedIndex(count);
		} else {
			_pinnedDialogs.push_back(key);
			if (_pinnedDialogs.size() > Global::PinnedDialogsCountMax()) {
				_pinnedDialogs.front().entry()->cachePinnedIndex(0);
				_pinnedDialogs.pop_front();

				auto index = 0;
				for (const auto &pinned : _pinnedDialogs) {
					pinned.entry()->cachePinnedIndex(++index);
				}
			} else {
				key.entry()->cachePinnedIndex(_pinnedDialogs.size());
			}
		}
	} else if (!pinned && already != _pinnedDialogs.end()) {
		key.entry()->cachePinnedIndex(0);
		_pinnedDialogs.erase(already);
		auto index = 0;
		for (const auto &pinned : _pinnedDialogs) {
			pinned.entry()->cachePinnedIndex(++index);
		}
	}
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

void Session::setMimeForwardIds(MessageIdsList &&list) {
	_mimeForwardIds = std::move(list);
}

MessageIdsList Session::takeMimeForwardIds() {
	return std::move(_mimeForwardIds);
}

} // namespace Data
