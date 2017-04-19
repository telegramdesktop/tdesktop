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
} // namespace Storage

namespace Window {
namespace Notifications {
class System;
} // namespace Notifications
} // namespace Window

namespace Calls {
class Instance;
} // namespace Calls

class ApiWrap;

enum class EmojiPanelTab {
	Emoji,
	Stickers,
	Gifs,
};

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
	base::Observable<void> &savedGifsUpdated() {
		return _savedGifsUpdated;
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
	EmojiPanelTab emojiPanelTab() const {
		return _variables.emojiPanelTab;
	}
	void setEmojiPanelTab(EmojiPanelTab tab) {
		_variables.emojiPanelTab = tab;
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

private:
	struct Variables {
		bool lastSeenWarningSeen = false;
		EmojiPanelTab emojiPanelTab = EmojiPanelTab::Emoji;
		bool tabbedSelectorSectionEnabled = true;
	};

	base::Variable<bool> _contactsLoaded = { false };
	base::Variable<bool> _allChatsLoaded = { false };
	base::Observable<void> _moreChatsLoaded;
	base::Observable<void> _savedGifsUpdated;
	Variables _variables;
	TimeMs _lastTimeVideoPlayedAt = 0;

};

class AuthSession final : private base::Subscriber {
public:
	AuthSession(UserId userId);

	AuthSession(const AuthSession &other) = delete;
	AuthSession &operator=(const AuthSession &other) = delete;

	static bool Exists();

	static AuthSession &Current();
	static UserId CurrentUserId() {
		return Current().userId();
	}
	static PeerId CurrentUserPeerId() {
		return peerFromUser(CurrentUserId());
	}
	static UserData *CurrentUser();

	UserId userId() const {
		return _userId;
	}
	bool validateSelf(const MTPUser &user);

	Storage::Downloader &downloader() {
		return *_downloader;
	}

	static base::Observable<void> &CurrentDownloaderTaskFinished();

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
	const std::unique_ptr<Window::Notifications::System> _notifications;

};
