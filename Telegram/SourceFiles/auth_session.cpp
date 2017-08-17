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
#include "storage/file_upload.h"
#include "storage/localstorage.h"
#include "storage/serialize_common.h"
#include "window/notifications_manager.h"
#include "platform/platform_specific.h"
#include "calls/calls_instance.h"
#include "window/section_widget.h"
#include "chat_helpers/tabbed_selector.h"

namespace {

constexpr auto kAutoLockTimeoutLateMs = TimeMs(3000);

} // namespace

AuthSessionData::Variables::Variables()
: selectorTab(ChatHelpers::SelectorTab::Emoji)
, floatPlayerColumn(Window::Column::Second)
, floatPlayerCorner(RectPart::TopRight) {
}

QByteArray AuthSessionData::serialize() const {
	auto size = sizeof(qint32) * 8;
	for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
		size += Serialize::stringSize(i.key()) + Serialize::stringSize(i.value());
	}
	size += _variables.groupStickersSectionHidden.size() * sizeof(quint64);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << static_cast<qint32>(_variables.selectorTab);
		stream << qint32(_variables.lastSeenWarningSeen ? 1 : 0);
		stream << qint32(_variables.tabbedSelectorSectionEnabled ? 1 : 0);
		stream << qint32(_variables.soundOverrides.size());
		for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
			stream << i.key() << i.value();
		}
		stream << qint32(_variables.tabbedSelectorSectionTooltipShown);
		stream << qint32(_variables.floatPlayerColumn);
		stream << qint32(_variables.floatPlayerCorner);
		stream << qint32(_variables.groupStickersSectionHidden.size());
		for (auto peerId : _variables.groupStickersSectionHidden) {
			stream << quint64(peerId);
		}
	}
	return result;
}

void AuthSessionData::constructFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 selectorTab = static_cast<qint32>(ChatHelpers::SelectorTab::Emoji);
	qint32 lastSeenWarningSeen = 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	qint32 tabbedSelectorSectionTooltipShown = 0;
	qint32 floatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 floatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	QMap<QString, QString> soundOverrides;
	OrderedSet<PeerId> groupStickersSectionHidden;
	stream >> selectorTab;
	stream >> lastSeenWarningSeen;
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionEnabled;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				QString key, value;
				stream >> key >> value;
				soundOverrides[key] = value;
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionTooltipShown;
	}
	if (!stream.atEnd()) {
		stream >> floatPlayerColumn >> floatPlayerCorner;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 peerId;
				stream >> peerId;
				groupStickersSectionHidden.insert(peerId);
			}
		}
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: Bad data for AuthSessionData::constructFromSerialized()"));
		return;
	}

	auto uncheckedTab = static_cast<ChatHelpers::SelectorTab>(selectorTab);
	switch (uncheckedTab) {
	case ChatHelpers::SelectorTab::Emoji:
	case ChatHelpers::SelectorTab::Stickers:
	case ChatHelpers::SelectorTab::Gifs: _variables.selectorTab = uncheckedTab; break;
	}
	_variables.lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_variables.tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
	_variables.soundOverrides = std::move(soundOverrides);
	_variables.tabbedSelectorSectionTooltipShown = tabbedSelectorSectionTooltipShown;
	auto uncheckedColumn = static_cast<Window::Column>(floatPlayerColumn);
	switch (uncheckedColumn) {
	case Window::Column::First:
	case Window::Column::Second:
	case Window::Column::Third: _variables.floatPlayerColumn = uncheckedColumn; break;
	}
	auto uncheckedCorner = static_cast<RectPart>(floatPlayerCorner);
	switch (uncheckedCorner) {
	case RectPart::TopLeft:
	case RectPart::TopRight:
	case RectPart::BottomLeft:
	case RectPart::BottomRight: _variables.floatPlayerCorner = uncheckedCorner; break;
	}
	_variables.groupStickersSectionHidden = std::move(groupStickersSectionHidden);
}

QString AuthSessionData::getSoundPath(const QString &key) const {
	auto it = _variables.soundOverrides.constFind(key);
	if (it != _variables.soundOverrides.end()) {
		return it.value();
	}
	return qsl(":/sounds/") + key + qsl(".mp3");
}

AuthSession &Auth() {
	auto result = Messenger::Instance().authSession();
	Assert(result != nullptr);
	return *result;
}

AuthSession::AuthSession(UserId userId)
: _userId(userId)
, _autoLockTimer([this] { checkAutoLock(); })
, _api(std::make_unique<ApiWrap>(this))
, _calls(std::make_unique<Calls::Instance>())
, _downloader(std::make_unique<Storage::Downloader>())
, _uploader(std::make_unique<Storage::Uploader>())
, _notifications(std::make_unique<Window::Notifications::System>(this)) {
	Expects(_userId != 0);
	_saveDataTimer.setCallback([this] {
		Local::writeUserSettings();
	});
	subscribe(Messenger::Instance().passcodedChanged(), [this] {
		_shouldLockAt = 0;
		notifications().updateAll();
	});
	_api->start();
}

bool AuthSession::Exists() {
	if (auto messenger = Messenger::InstancePointer()) {
		return (messenger->authSession() != nullptr);
	}
	return false;
}

UserData *AuthSession::user() const {
	return App::user(userId());
}

base::Observable<void> &AuthSession::downloaderTaskFinished() {
	return downloader().taskFinished();
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
	Expects(this == &Auth());
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
