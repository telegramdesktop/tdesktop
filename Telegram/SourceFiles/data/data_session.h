/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/stickers.h"
#include "dialogs/dialogs_key.h"

struct HistoryMessageGroup;

namespace Data {

class Feed;

class Session final {
public:
	Session();
	~Session();

	base::Variable<bool> &contactsLoaded() {
		return _contactsLoaded;
	}
	base::Variable<bool> &allChatsLoaded() {
		return _allChatsLoaded;
	}
	base::Observable<void> &moreChatsLoaded() {
		return _moreChatsLoaded;
	}
	base::Observable<void> &pendingHistoryResize() {
		return _pendingHistoryResize;
	}
	struct ItemVisibilityQuery {
		not_null<HistoryItem*> item;
		not_null<bool*> isVisible;
	};
	base::Observable<ItemVisibilityQuery> &queryItemVisibility() {
		return _queryItemVisibility;
	}
	void markItemLayoutChanged(not_null<const HistoryItem*> item);
	rpl::producer<not_null<const HistoryItem*>> itemLayoutChanged() const;
	void requestItemRepaint(not_null<const HistoryItem*> item);
	rpl::producer<not_null<const HistoryItem*>> itemRepaintRequest() const;
	void markItemRemoved(not_null<const HistoryItem*> item);
	rpl::producer<not_null<const HistoryItem*>> itemRemoved() const;
	void markHistoryUnloaded(not_null<const History*> history);
	rpl::producer<not_null<const History*>> historyUnloaded() const;
	void markHistoryCleared(not_null<const History*> history);
	rpl::producer<not_null<const History*>> historyCleared() const;
	using MegagroupParticipant = std::tuple<
		not_null<ChannelData*>,
		not_null<UserData*>>;
	void removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	rpl::producer<MegagroupParticipant> megagroupParticipantRemoved() const;
	rpl::producer<not_null<UserData*>> megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const;
	void addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	rpl::producer<MegagroupParticipant> megagroupParticipantAdded() const;
	rpl::producer<not_null<UserData*>> megagroupParticipantAdded(
		not_null<ChannelData*> channel) const;

	void markStickersUpdated();
	rpl::producer<> stickersUpdated() const;
	void markSavedGifsUpdated();
	rpl::producer<> savedGifsUpdated() const;

	bool stickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastStickersUpdate, now);
	}
	void setLastStickersUpdate(TimeMs update) {
		_lastStickersUpdate = update;
	}
	bool recentStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastRecentStickersUpdate, now);
	}
	void setLastRecentStickersUpdate(TimeMs update) {
		_lastRecentStickersUpdate = update;
	}
	bool favedStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastFavedStickersUpdate, now);
	}
	void setLastFavedStickersUpdate(TimeMs update) {
		_lastFavedStickersUpdate = update;
	}
	bool featuredStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastFeaturedStickersUpdate, now);
	}
	void setLastFeaturedStickersUpdate(TimeMs update) {
		_lastFeaturedStickersUpdate = update;
	}
	bool savedGifsUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastSavedGifsUpdate, now);
	}
	void setLastSavedGifsUpdate(TimeMs update) {
		_lastSavedGifsUpdate = update;
	}
	int featuredStickerSetsUnreadCount() const {
		return _featuredStickerSetsUnreadCount.current();
	}
	void setFeaturedStickerSetsUnreadCount(int count) {
		_featuredStickerSetsUnreadCount = count;
	}
	rpl::producer<int> featuredStickerSetsUnreadCountValue() const {
		return _featuredStickerSetsUnreadCount.value();
	}
	const Stickers::Sets &stickerSets() const {
		return _stickerSets;
	}
	Stickers::Sets &stickerSetsRef() {
		return _stickerSets;
	}
	const Stickers::Order &stickerSetsOrder() const {
		return _stickerSetsOrder;
	}
	Stickers::Order &stickerSetsOrderRef() {
		return _stickerSetsOrder;
	}
	const Stickers::Order &featuredStickerSetsOrder() const {
		return _featuredStickerSetsOrder;
	}
	Stickers::Order &featuredStickerSetsOrderRef() {
		return _featuredStickerSetsOrder;
	}
	const Stickers::Order &archivedStickerSetsOrder() const {
		return _archivedStickerSetsOrder;
	}
	Stickers::Order &archivedStickerSetsOrderRef() {
		return _archivedStickerSetsOrder;
	}
	const Stickers::SavedGifs &savedGifs() const {
		return _savedGifs;
	}
	Stickers::SavedGifs &savedGifsRef() {
		return _savedGifs;
	}

	HistoryItemsList idsToItems(const MessageIdsList &ids) const;
	MessageIdsList itemsToIds(const HistoryItemsList &items) const;
	MessageIdsList groupToIds(not_null<HistoryMessageGroup*> group) const;
	MessageIdsList itemOrItsGroup(not_null<HistoryItem*> item) const;

	int pinnedDialogsCount() const;
	const std::deque<Dialogs::Key> &pinnedDialogsOrder() const;
	void setPinnedDialog(const Dialogs::Key &key, bool pinned);
	void applyPinnedDialogs(const QVector<MTPDialog> &list);
	void applyPinnedDialogs(const QVector<MTPDialogPeer> &list);
	void reorderTwoPinnedDialogs(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2);

	not_null<Data::Feed*> feed(FeedId id);
	Data::Feed *feedLoaded(FeedId id);

	void setMimeForwardIds(MessageIdsList &&list);
	MessageIdsList takeMimeForwardIds();

private:
	bool stickersUpdateNeeded(TimeMs lastUpdate, TimeMs now) const {
		constexpr auto kStickersUpdateTimeout = TimeMs(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kStickersUpdateTimeout);
	}
	void userIsContactUpdated(not_null<UserData*> user);

	void clearPinnedDialogs();
	void setIsPinned(const Dialogs::Key &key, bool pinned);

	base::Variable<bool> _contactsLoaded = { false };
	base::Variable<bool> _allChatsLoaded = { false };
	base::Observable<void> _moreChatsLoaded;
	base::Observable<void> _pendingHistoryResize;
	base::Observable<ItemVisibilityQuery> _queryItemVisibility;
	rpl::event_stream<not_null<const HistoryItem*>> _itemLayoutChanged;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRepaintRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRemoved;
	rpl::event_stream<not_null<const History*>> _historyUnloaded;
	rpl::event_stream<not_null<const History*>> _historyCleared;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantRemoved;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantAdded;

	rpl::event_stream<> _stickersUpdated;
	rpl::event_stream<> _savedGifsUpdated;
	TimeMs _lastStickersUpdate = 0;
	TimeMs _lastRecentStickersUpdate = 0;
	TimeMs _lastFavedStickersUpdate = 0;
	TimeMs _lastFeaturedStickersUpdate = 0;
	TimeMs _lastSavedGifsUpdate = 0;
	rpl::variable<int> _featuredStickerSetsUnreadCount = 0;
	Stickers::Sets _stickerSets;
	Stickers::Order _stickerSetsOrder;
	Stickers::Order _featuredStickerSetsOrder;
	Stickers::Order _archivedStickerSetsOrder;
	Stickers::SavedGifs _savedGifs;

	std::deque<Dialogs::Key> _pinnedDialogs;
	base::flat_map<FeedId, std::unique_ptr<Data::Feed>> _feeds;

	MessageIdsList _mimeForwardIds;

	rpl::lifetime _lifetime;

};

} // namespace Data
