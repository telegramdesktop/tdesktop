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
#include "auth_session.h"

#include "apiwrap.h"
#include "messenger.h"
#include "storage/file_download.h"
#include "storage/localstorage.h"
#include "window/notifications_manager.h"
#include "platform/platform_specific.h"
#include "calls/calls_instance.h"

namespace {

constexpr auto kAutoLockTimeoutLateMs = TimeMs(3000);

} // namespace

QByteArray AuthSessionData::serialize() const {
	auto size = sizeof(qint32) * 2;

	auto result = QByteArray();
	result.reserve(size);
	{
		QBuffer buffer(&result);
		if (!buffer.open(QIODevice::WriteOnly)) {
			Unexpected("Can't open data for AuthSessionData::serialize()");
		}

		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << static_cast<qint32>(_variables.emojiPanelTab);
		stream << qint32(_variables.lastSeenWarningSeen ? 1 : 0);
		stream << qint32(_variables.tabbedSelectorSectionEnabled ? 1 : 0);
	}
	return result;
}

void AuthSessionData::constructFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	auto readonly = serialized;
	QBuffer buffer(&readonly);
	if (!buffer.open(QIODevice::ReadOnly)) {
		Unexpected("Can't open data for DcOptions::constructFromSerialized()");
	}
	QDataStream stream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 emojiPanTab = static_cast<qint32>(EmojiPanelTab::Emoji);
	qint32 lastSeenWarningSeen = 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	stream >> emojiPanTab;
	stream >> lastSeenWarningSeen;
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionEnabled;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: Bad data for AuthSessionData::constructFromSerialized()"));
		return;
	}

	auto uncheckedTab = static_cast<EmojiPanelTab>(emojiPanTab);
	switch (uncheckedTab) {
	case EmojiPanelTab::Emoji:
	case EmojiPanelTab::Stickers:
	case EmojiPanelTab::Gifs: _variables.emojiPanelTab = uncheckedTab; break;
	}
	_variables.lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_variables.tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
}

AuthSession::AuthSession(UserId userId)
: _userId(userId)
, _autoLockTimer([this] { checkAutoLock(); })
, _api(std::make_unique<ApiWrap>())
, _calls(std::make_unique<Calls::Instance>())
, _downloader(std::make_unique<Storage::Downloader>())
, _notifications(std::make_unique<Window::Notifications::System>(this)) {
	Expects(_userId != 0);
	_saveDataTimer.setCallback([this] {
		Local::writeUserSettings();
	});
	subscribe(Messenger::Instance().passcodedChanged(), [this] {
		_shouldLockAt = 0;
		notifications().updateAll();
	});
}

bool AuthSession::Exists() {
	if (auto messenger = Messenger::InstancePointer()) {
		return (messenger->authSession() != nullptr);
	}
	return false;
}

AuthSession &AuthSession::Current() {
	auto result = Messenger::Instance().authSession();
	t_assert(result != nullptr);
	return *result;
}

UserData *AuthSession::CurrentUser() {
	return App::user(CurrentUserId());
}

base::Observable<void> &AuthSession::CurrentDownloaderTaskFinished() {
	return Current().downloader().taskFinished();
}

bool AuthSession::validateSelf(const MTPUser &user) {
	if (user.type() != mtpc_user || !user.c_user().is_self() || user.c_user().vid.v != userId()) {
		LOG(("Auth Error: wrong self user received."));
		App::logOutDelayed();
		return false;
	}
	return true;
}

void AuthSession::saveDataDelayed(TimeMs delay) {
	Expects(this == &AuthSession::Current());
	_saveDataTimer.callOnce(delay);
}

void AuthSession::checkAutoLock() {
	if (!Global::LocalPasscode() || App::passcoded()) return;

	Messenger::Instance().checkLocalTime();
	auto now = getms(true);
	auto shouldLockInMs = Global::AutoLock() * 1000LL;
	auto idleForMs = psIdleTime();
	auto notPlayingVideoForMs = now - data().lastTimeVideoPlayedAt();
	auto checkTimeMs = qMin(idleForMs, notPlayingVideoForMs);
	if (checkTimeMs >= shouldLockInMs || (_shouldLockAt > 0 && now > _shouldLockAt + kAutoLockTimeoutLateMs)) {
		Messenger::Instance().setupPasscode();
	} else {
		_shouldLockAt = now + (shouldLockInMs - checkTimeMs);
		_autoLockTimer.callOnce(shouldLockInMs - checkTimeMs);
	}
}

void AuthSession::checkAutoLockIn(TimeMs time) {
	if (_autoLockTimer.isActive()) {
		auto remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= time) return;
	}
	_autoLockTimer.callOnce(time);
}

AuthSession::~AuthSession() = default;
