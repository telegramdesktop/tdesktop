/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <rpl/event_stream.h>
#include <rpl/filter.h>
#include <rpl/variable.h>
#include "base/timer.h"
#include "chat_helpers/stickers.h"

namespace Storage {
class Downloader;
class Uploader;
class Facade;
} // namespace Storage

namespace Window {
namespace Notifications {
class System;
} // namespace Notifications
enum class Column;
} // namespace Window

namespace Calls {
class Instance;
} // namespace Calls

namespace ChatHelpers {
enum class SelectorTab;
} // namespace ChatHelpers

class ApiWrap;

class AuthSessionData final {
public:
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

	void moveFrom(AuthSessionData &&other) {
		_variables = std::move(other._variables);
	}
	QByteArray serialize() const;
	void constructFromSerialized(const QByteArray &serialized);

	bool lastSeenWarningSeen() const {
		return _variables.lastSeenWarningSeen;
	}
	void setLastSeenWarningSeen(bool lastSeenWarningSeen) {
		_variables.lastSeenWarningSeen = lastSeenWarningSeen;
	}
	ChatHelpers::SelectorTab selectorTab() const {
		return _variables.selectorTab;
	}
	void setSelectorTab(ChatHelpers::SelectorTab tab) {
		_variables.selectorTab = tab;
	}
	bool tabbedSelectorSectionEnabled() const {
		return _variables.tabbedSelectorSectionEnabled;
	}
	void setTabbedSelectorSectionEnabled(bool enabled);
	bool thirdSectionInfoEnabled() const {
		return _variables.thirdSectionInfoEnabled;
	}
	void setThirdSectionInfoEnabled(bool enabled);
	rpl::producer<bool> thirdSectionInfoEnabledValue() const;
	int thirdSectionExtendedBy() const {
		return _variables.thirdSectionExtendedBy;
	}
	void setThirdSectionExtendedBy(int savedValue) {
		_variables.thirdSectionExtendedBy = savedValue;
	}
	bool tabbedReplacedWithInfo() const {
		return _tabbedReplacedWithInfo;
	}
	void setTabbedReplacedWithInfo(bool enabled);
	rpl::producer<bool> tabbedReplacedWithInfoValue() const;
	void setSmallDialogsList(bool enabled) {
		_variables.smallDialogsList = enabled;
	}
	bool smallDialogsList() const {
		return _variables.smallDialogsList;
	}
	void setLastTimeVideoPlayedAt(TimeMs time) {
		_lastTimeVideoPlayedAt = time;
	}
	TimeMs lastTimeVideoPlayedAt() const {
		return _lastTimeVideoPlayedAt;
	}
	void setSoundOverride(const QString &key, const QString &path) {
		_variables.soundOverrides.insert(key, path);
	}
	void clearSoundOverrides() {
		_variables.soundOverrides.clear();
	}
	QString getSoundPath(const QString &key) const;
	void setTabbedSelectorSectionTooltipShown(int shown) {
		_variables.tabbedSelectorSectionTooltipShown = shown;
	}
	int tabbedSelectorSectionTooltipShown() const {
		return _variables.tabbedSelectorSectionTooltipShown;
	}
	void setFloatPlayerColumn(Window::Column column) {
		_variables.floatPlayerColumn = column;
	}
	Window::Column floatPlayerColumn() const {
		return _variables.floatPlayerColumn;
	}
	void setFloatPlayerCorner(RectPart corner) {
		_variables.floatPlayerCorner = corner;
	}
	RectPart floatPlayerCorner() const {
		return _variables.floatPlayerCorner;
	}
	void setDialogsWidthRatio(float64 ratio);
	float64 dialogsWidthRatio() const;
	rpl::producer<float64> dialogsWidthRatioChanges() const;
	void setThirdColumnWidth(int width);
	int thirdColumnWidth() const;
	rpl::producer<int> thirdColumnWidthChanges() const;

	void markStickersUpdated();
	rpl::producer<> stickersUpdated() const;
	void markSavedGifsUpdated();
	rpl::producer<> savedGifsUpdated() const;
	void setGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.insert(peerId);
	}
	bool isGroupStickersSectionHidden(PeerId peerId) const {
		return _variables.groupStickersSectionHidden.contains(peerId);
	}
	void removeGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.remove(peerId);
	}
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

private:
	struct Variables {
		Variables();

		static constexpr auto kDefaultDialogsWidthRatio = 5. / 14;
		static constexpr auto kDefaultThirdColumnWidth = 0;

		bool lastSeenWarningSeen = false;
		ChatHelpers::SelectorTab selectorTab; // per-window
		bool tabbedSelectorSectionEnabled = false; // per-window
		int tabbedSelectorSectionTooltipShown = 0;
		QMap<QString, QString> soundOverrides;
		Window::Column floatPlayerColumn; // per-window
		RectPart floatPlayerCorner; // per-window
		base::flat_set<PeerId> groupStickersSectionHidden;
		bool thirdSectionInfoEnabled = true; // per-window
		bool smallDialogsList = false; // per-window
		int thirdSectionExtendedBy = -1; // per-window
		rpl::variable<float64> dialogsWidthRatio
			= kDefaultDialogsWidthRatio; // per-window
		rpl::variable<int> thirdColumnWidth
			= kDefaultThirdColumnWidth; // per-window
	};

	bool stickersUpdateNeeded(TimeMs lastUpdate, TimeMs now) const {
		constexpr auto kStickersUpdateTimeout = TimeMs(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kStickersUpdateTimeout);
	}

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

	rpl::event_stream<bool> _thirdSectionInfoEnabledValue;
	bool _tabbedReplacedWithInfo = false;
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue;

	Variables _variables;
	TimeMs _lastTimeVideoPlayedAt = 0;

};

// One per Messenger.
class AuthSession;
AuthSession &Auth();

class AuthSession final : private base::Subscriber {
public:
	AuthSession(UserId userId);

	AuthSession(const AuthSession &other) = delete;
	AuthSession &operator=(const AuthSession &other) = delete;

	static bool Exists();

	UserId userId() const {
		return _userId;
	}
	PeerId userPeerId() const {
		return peerFromUser(userId());
	}
	UserData *user() const;
	bool validateSelf(const MTPUser &user);

	Storage::Downloader &downloader() {
		return *_downloader;
	}
	Storage::Uploader &uploader() {
		return *_uploader;
	}
	Storage::Facade &storage() {
		return *_storage;
	}

	base::Observable<void> &downloaderTaskFinished();

	Window::Notifications::System &notifications() {
		return *_notifications;
	}

	AuthSessionData &data() {
		return _data;
	}
	void saveDataDelayed(TimeMs delay = kDefaultSaveDelay);

	ApiWrap &api() {
		return *_api;
	}

	Calls::Instance &calls() {
		return *_calls;
	}

	void checkAutoLock();
	void checkAutoLockIn(TimeMs time);

	base::Observable<DocumentData*> documentUpdated;
	base::Observable<std::pair<not_null<HistoryItem*>, MsgId>> messageIdChanging;

	~AuthSession();

private:
	static constexpr auto kDefaultSaveDelay = TimeMs(1000);

	const UserId _userId = 0;
	AuthSessionData _data;
	base::Timer _saveDataTimer;

	TimeMs _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<Calls::Instance> _calls;
	const std::unique_ptr<Storage::Downloader> _downloader;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Storage::Facade> _storage;
	const std::unique_ptr<Window::Notifications::System> _notifications;

};
