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

#include "base/timer.h"

namespace Storage {
class Downloader;
class Uploader;
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
	base::Observable<void> &stickersUpdated() {
		return _stickersUpdated;
	}
	base::Observable<void> &savedGifsUpdated() {
		return _savedGifsUpdated;
	}
	base::Observable<not_null<History*>> &historyCleared() {
		return _historyCleared;
	}
	base::Observable<not_null<const HistoryItem*>> &repaintLogEntry() {
		return _repaintLogEntry;
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

	void copyFrom(const AuthSessionData &other) {
		_variables = other._variables;
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
	void setTabbedSelectorSectionEnabled(bool enabled) {
		_variables.tabbedSelectorSectionEnabled = enabled;
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
	void setGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.insert(peerId);
	}
	bool isGroupStickersSectionHidden(PeerId peerId) const {
		return _variables.groupStickersSectionHidden.contains(peerId);
	}
	void removeGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.remove(peerId);
	}

private:
	struct Variables {
		Variables();

		bool lastSeenWarningSeen = false;
		ChatHelpers::SelectorTab selectorTab;
		bool tabbedSelectorSectionEnabled = true;
		int tabbedSelectorSectionTooltipShown = 0;
		QMap<QString, QString> soundOverrides;
		Window::Column floatPlayerColumn;
		RectPart floatPlayerCorner;
		OrderedSet<PeerId> groupStickersSectionHidden;
	};

	base::Variable<bool> _contactsLoaded = { false };
	base::Variable<bool> _allChatsLoaded = { false };
	base::Observable<void> _moreChatsLoaded;
	base::Observable<void> _stickersUpdated;
	base::Observable<void> _savedGifsUpdated;
	base::Observable<not_null<History*>> _historyCleared;
	base::Observable<not_null<const HistoryItem*>> _repaintLogEntry;
	base::Observable<void> _pendingHistoryResize;
	base::Observable<ItemVisibilityQuery> _queryItemVisibility;
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

	base::Observable<void> &downloaderTaskFinished();

	Window::Notifications::System &notifications() {
		return *_notifications;
	}

	AuthSessionData &data() {
		return _data;
	}
	void saveDataDelayed(TimeMs delay);

	ApiWrap &api() {
		return *_api;
	}

	Calls::Instance &calls() {
		return *_calls;
	}

	void checkAutoLock();
	void checkAutoLockIn(TimeMs time);

	base::Observable<DocumentData*> documentUpdated;
	base::Observable<std::pair<HistoryItem*, MsgId>> messageIdChanging;

	~AuthSession();

private:
	const UserId _userId = 0;
	AuthSessionData _data;
	base::Timer _saveDataTimer;

	TimeMs _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<Calls::Instance> _calls;
	const std::unique_ptr<Storage::Downloader> _downloader;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Window::Notifications::System> _notifications;

};
