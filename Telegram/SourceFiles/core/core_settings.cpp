/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings.h"

#include "boxes/send_files_box.h"
#include "ui/widgets/input_fields.h"
#include "storage/serialize_common.h"
#include "facades.h"

namespace Core {

Settings::Settings()
: _sendFilesWay(SendFilesWay::Album)
, _sendSubmitWay(Ui::InputSubmitSettings::Enter) {
}

QByteArray Settings::serialize() const {
	const auto themesAccentColors = _themesAccentColors.serialize();
	auto size = Serialize::bytearraySize(themesAccentColors)
		+ sizeof(qint32) * 5
		+ Serialize::stringSize(_downloadPath.current())
		+ Serialize::bytearraySize(_downloadPathBookmark)
		+ sizeof(qint32) * 12
		+ Serialize::stringSize(_callOutputDeviceID)
		+ Serialize::stringSize(_callInputDeviceID)
		+ sizeof(qint32) * 3;
	for (const auto &[key, value] : _soundOverrides) {
		size += Serialize::stringSize(key) + Serialize::stringSize(value);
	}
	size += Serialize::bytearraySize(_videoPipGeometry);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< themesAccentColors
			<< qint32(_adaptiveForWide ? 1 : 0)
			<< qint32(_moderateModeEnabled ? 1 : 0)
			<< qint32(qRound(_songVolume.current() * 1e6))
			<< qint32(qRound(_videoVolume.current() * 1e6))
			<< qint32(_askDownloadPath ? 1 : 0)
			<< _downloadPath.current()
			<< _downloadPathBookmark
			<< qint32(_voiceMsgPlaybackDoubled ? 1 : 0)
			<< qint32(_soundNotify ? 1 : 0)
			<< qint32(_desktopNotify ? 1 : 0)
			<< qint32(_flashBounceNotify ? 1 : 0)
			<< static_cast<qint32>(_notifyView)
			<< qint32(_nativeNotifications ? 1 : 0)
			<< qint32(_notificationsCount)
			<< static_cast<qint32>(_notificationsCorner)
			<< qint32(_autoLock)
			<< _callOutputDeviceID
			<< _callInputDeviceID
			<< qint32(_callOutputVolume)
			<< qint32(_callInputVolume)
			<< qint32(_callAudioDuckingEnabled ? 1 : 0)
			<< qint32(_lastSeenWarningSeen ? 1 : 0)
			<< qint32(_soundOverrides.size());
		for (const auto &[key, value] : _soundOverrides) {
			stream << key << value;
		}
		stream
			<< qint32(_sendFilesWay)
			<< qint32(_sendSubmitWay)
			<< qint32(_includeMutedCounter ? 1 : 0)
			<< qint32(_countUnreadMessages ? 1 : 0)
			<< qint32(_exeLaunchWarning ? 1 : 0)
			<< qint32(_notifyAboutPinned.current() ? 1 : 0)
			<< qint32(_loopAnimatedStickers ? 1 : 0)
			<< qint32(_largeEmoji.current() ? 1 : 0)
			<< qint32(_replaceEmoji.current() ? 1 : 0)
			<< qint32(_suggestEmoji ? 1 : 0)
			<< qint32(_suggestStickersByEmoji ? 1 : 0)
			<< qint32(_spellcheckerEnabled.current() ? 1 : 0)
			<< qint32(SerializePlaybackSpeed(_videoPlaybackSpeed.current()))
			<< _videoPipGeometry
			<< qint32(_dictionariesEnabled.current().size());
		for (const auto i : _dictionariesEnabled.current()) {
			stream << quint64(i);
		}
		stream
			<< qint32(_autoDownloadDictionaries.current() ? 1 : 0);

	}
	return result;
}

void Settings::addFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	QByteArray themesAccentColors;
	qint32 adaptiveForWide = _adaptiveForWide ? 1 : 0;
	qint32 moderateModeEnabled = _moderateModeEnabled ? 1 : 0;
	qint32 songVolume = qint32(qRound(_songVolume.current() * 1e6));
	qint32 videoVolume = qint32(qRound(_videoVolume.current() * 1e6));
	qint32 askDownloadPath = _askDownloadPath ? 1 : 0;
	QString downloadPath = _downloadPath.current();
	QByteArray downloadPathBookmark = _downloadPathBookmark;
	qint32 voiceMsgPlaybackDoubled = _voiceMsgPlaybackDoubled ? 1 : 0;
	qint32 soundNotify = _soundNotify ? 1 : 0;
	qint32 desktopNotify = _desktopNotify ? 1 : 0;
	qint32 flashBounceNotify = _flashBounceNotify ? 1 : 0;
	qint32 notifyView = static_cast<qint32>(_notifyView);
	qint32 nativeNotifications = _nativeNotifications ? 1 : 0;
	qint32 notificationsCount = _notificationsCount;
	qint32 notificationsCorner = static_cast<qint32>(_notificationsCorner);
	qint32 autoLock = _autoLock;
	QString callOutputDeviceID = _callOutputDeviceID;
	QString callInputDeviceID = _callInputDeviceID;
	qint32 callOutputVolume = _callOutputVolume;
	qint32 callInputVolume = _callInputVolume;
	qint32 callAudioDuckingEnabled = _callAudioDuckingEnabled ? 1 : 0;
	qint32 lastSeenWarningSeen = _lastSeenWarningSeen ? 1 : 0;
	qint32 soundOverridesCount = 0;
	base::flat_map<QString, QString> soundOverrides;
	qint32 sendFilesWay = static_cast<qint32>(_sendFilesWay);
	qint32 sendSubmitWay = static_cast<qint32>(_sendSubmitWay);
	qint32 includeMutedCounter = _includeMutedCounter ? 1 : 0;
	qint32 countUnreadMessages = _countUnreadMessages ? 1 : 0;
	qint32 exeLaunchWarning = _exeLaunchWarning ? 1 : 0;
	qint32 notifyAboutPinned = _notifyAboutPinned.current() ? 1 : 0;
	qint32 loopAnimatedStickers = _loopAnimatedStickers ? 1 : 0;
	qint32 largeEmoji = _largeEmoji.current() ? 1 : 0;
	qint32 replaceEmoji = _replaceEmoji.current() ? 1 : 0;
	qint32 suggestEmoji = _suggestEmoji ? 1 : 0;
	qint32 suggestStickersByEmoji = _suggestStickersByEmoji ? 1 : 0;
	qint32 spellcheckerEnabled = _spellcheckerEnabled.current() ? 1 : 0;
	qint32 videoPlaybackSpeed = Core::Settings::SerializePlaybackSpeed(_videoPlaybackSpeed.current());
	QByteArray videoPipGeometry = _videoPipGeometry;
	qint32 dictionariesEnabledCount = 0;
	std::vector<int> dictionariesEnabled;
	qint32 autoDownloadDictionaries = _autoDownloadDictionaries.current() ? 1 : 0;

	stream >> themesAccentColors;
	if (!stream.atEnd()) {
		stream
			>> adaptiveForWide
			>> moderateModeEnabled
			>> songVolume
			>> videoVolume
			>> askDownloadPath
			>> downloadPath
			>> downloadPathBookmark
			>> voiceMsgPlaybackDoubled
			>> soundNotify
			>> desktopNotify
			>> flashBounceNotify
			>> notifyView
			>> nativeNotifications
			>> notificationsCount
			>> notificationsCorner
			>> autoLock
			>> callOutputDeviceID
			>> callInputDeviceID
			>> callOutputVolume
			>> callInputVolume
			>> callAudioDuckingEnabled
			>> lastSeenWarningSeen
			>> soundOverridesCount;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != soundOverridesCount; ++i) {
				QString key, value;
				stream >> key >> value;
				soundOverrides.emplace(key, value);
			}
		}
		stream
			>> sendFilesWay
			>> sendSubmitWay
			>> includeMutedCounter
			>> countUnreadMessages
			>> exeLaunchWarning
			>> notifyAboutPinned
			>> loopAnimatedStickers
			>> largeEmoji
			>> replaceEmoji
			>> suggestEmoji
			>> suggestStickersByEmoji
			>> spellcheckerEnabled
			>> videoPlaybackSpeed
			>> videoPipGeometry
			>> dictionariesEnabledCount;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != dictionariesEnabledCount; ++i) {
				qint64 langId;
				stream >> langId;
				dictionariesEnabled.emplace_back(langId);
			}
		}
		stream
			>> autoDownloadDictionaries;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for Core::Settings::constructFromSerialized()"));
		return;
	} else if (!_themesAccentColors.setFromSerialized(themesAccentColors)) {
		return;
	}
	_adaptiveForWide = (adaptiveForWide == 1);
	_moderateModeEnabled = (moderateModeEnabled == 1);
	_songVolume = std::clamp(songVolume / 1e6, 0., 1.);
	_videoVolume = std::clamp(videoVolume / 1e6, 0., 1.);
	_askDownloadPath = (askDownloadPath == 1);
	_downloadPath = downloadPath;
	_downloadPathBookmark = downloadPathBookmark;
	_voiceMsgPlaybackDoubled = (voiceMsgPlaybackDoubled == 1);
	_soundNotify = (soundNotify == 1);
	_desktopNotify = (desktopNotify == 1);
	_flashBounceNotify = (flashBounceNotify == 1);
	const auto uncheckedNotifyView = static_cast<DBINotifyView>(notifyView);
	switch (uncheckedNotifyView) {
	case dbinvShowNothing:
	case dbinvShowName:
	case dbinvShowPreview: _notifyView = uncheckedNotifyView; break;
	}
	_nativeNotifications = (nativeNotifications == 1);
	_notificationsCount = (notificationsCount > 0) ? notificationsCount : 3;
	const auto uncheckedNotificationsCorner = static_cast<ScreenCorner>(notificationsCorner);
	switch (uncheckedNotificationsCorner) {
	case ScreenCorner::TopLeft:
	case ScreenCorner::TopRight:
	case ScreenCorner::BottomRight:
	case ScreenCorner::BottomLeft: _notificationsCorner = uncheckedNotificationsCorner; break;
	}
	_includeMutedCounter = (includeMutedCounter == 1);
	_countUnreadMessages = (countUnreadMessages == 1);
	_notifyAboutPinned = (notifyAboutPinned == 1);
	_autoLock = autoLock;
	_callOutputDeviceID = callOutputDeviceID;
	_callInputDeviceID = callInputDeviceID;
	_callOutputVolume = callOutputVolume;
	_callInputVolume = callInputVolume;
	_callAudioDuckingEnabled = (callAudioDuckingEnabled == 1);
	_lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_soundOverrides = std::move(soundOverrides);
	auto uncheckedSendFilesWay = static_cast<SendFilesWay>(sendFilesWay);
	switch (uncheckedSendFilesWay) {
	case SendFilesWay::Album:
	case SendFilesWay::Photos:
	case SendFilesWay::Files: _sendFilesWay = uncheckedSendFilesWay; break;
	}
	auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(sendSubmitWay);
	switch (uncheckedSendSubmitWay) {
	case Ui::InputSubmitSettings::Enter:
	case Ui::InputSubmitSettings::CtrlEnter: _sendSubmitWay = uncheckedSendSubmitWay; break;
	}
	_includeMutedCounter = (includeMutedCounter == 1);
	_countUnreadMessages = (countUnreadMessages == 1);
	_exeLaunchWarning = (exeLaunchWarning == 1);
	_notifyAboutPinned = (notifyAboutPinned == 1);
	_loopAnimatedStickers = (loopAnimatedStickers == 1);
	_largeEmoji = (largeEmoji == 1);
	_replaceEmoji = (replaceEmoji == 1);
	_suggestEmoji = (suggestEmoji == 1);
	_suggestStickersByEmoji = (suggestStickersByEmoji == 1);
	_spellcheckerEnabled = (spellcheckerEnabled == 1);
	_videoPlaybackSpeed = DeserializePlaybackSpeed(videoPlaybackSpeed);
	_videoPipGeometry = (videoPipGeometry);
	_dictionariesEnabled = std::move(dictionariesEnabled);
	_autoDownloadDictionaries = (autoDownloadDictionaries == 1);
}

bool Settings::chatWide() const {
	return _adaptiveForWide
		&& (Global::AdaptiveChatLayout() == Adaptive::ChatLayout::Wide);
}

QString Settings::getSoundPath(const QString &key) const {
	auto it = _soundOverrides.find(key);
	if (it != _soundOverrides.end()) {
		return it->second;
	}
	return qsl(":/sounds/") + key + qsl(".mp3");
}

} // namespace Core
