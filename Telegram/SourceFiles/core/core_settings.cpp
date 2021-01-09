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
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "base/platform/base_platform_info.h"
#include "webrtc/webrtc_create_adm.h"
#include "facades.h"

namespace Core {

Settings::Settings()
: _callAudioBackend(Webrtc::Backend::OpenAL)
, _sendSubmitWay(Ui::InputSubmitSettings::Enter)
, _floatPlayerColumn(Window::Column::Second)
, _floatPlayerCorner(RectPart::TopRight)
, _dialogsWidthRatio(DefaultDialogsWidthRatio()) {
}

QByteArray Settings::serialize() const {
	const auto themesAccentColors = _themesAccentColors.serialize();
	auto size = Serialize::bytearraySize(themesAccentColors)
		+ sizeof(qint32) * 5
		+ Serialize::stringSize(_downloadPath.current())
		+ Serialize::bytearraySize(_downloadPathBookmark)
		+ sizeof(qint32) * 12
		+ Serialize::stringSize(_callOutputDeviceId)
		+ Serialize::stringSize(_callInputDeviceId)
		+ Serialize::stringSize(_callVideoInputDeviceId)
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
			<< _callOutputDeviceId
			<< _callInputDeviceId
			<< qint32(_callOutputVolume)
			<< qint32(_callInputVolume)
			<< qint32(_callAudioDuckingEnabled ? 1 : 0)
			<< qint32(_lastSeenWarningSeen ? 1 : 0)
			<< qint32(_soundOverrides.size());
		for (const auto &[key, value] : _soundOverrides) {
			stream << key << value;
		}
		stream
			<< qint32(_sendFilesWay.serialize())
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
			<< qint32(_autoDownloadDictionaries.current() ? 1 : 0)
			<< qint32(_mainMenuAccountsShown.current() ? 1 : 0)
			<< qint32(_tabbedSelectorSectionEnabled ? 1 : 0)
			<< qint32(_floatPlayerColumn)
			<< qint32(_floatPlayerCorner)
			<< qint32(_thirdSectionInfoEnabled ? 1 : 0)
			<< qint32(snap(
				qRound(_dialogsWidthRatio.current() * 1000000),
				0,
				1000000))
			<< qint32(_thirdColumnWidth.current())
			<< qint32(_thirdSectionExtendedBy)
			<< qint32(_notifyFromAll ? 1 : 0)
			<< qint32(_nativeWindowFrame.current() ? 1 : 0)
			<< qint32(_systemDarkModeEnabled.current() ? 1 : 0)
			<< _callVideoInputDeviceId
			<< qint32(_ipRevealWarning ? 1 : 0)
			<< qint32(_groupCallPushToTalk ? 1 : 0)
			<< _groupCallPushToTalkShortcut
			<< qint64(_groupCallPushToTalkDelay)
			<< qint32(_callAudioBackend);
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
	QString callOutputDeviceId = _callOutputDeviceId;
	QString callInputDeviceId = _callInputDeviceId;
	QString callVideoInputDeviceId = _callVideoInputDeviceId;
	qint32 callOutputVolume = _callOutputVolume;
	qint32 callInputVolume = _callInputVolume;
	qint32 callAudioDuckingEnabled = _callAudioDuckingEnabled ? 1 : 0;
	qint32 lastSeenWarningSeen = _lastSeenWarningSeen ? 1 : 0;
	qint32 soundOverridesCount = 0;
	base::flat_map<QString, QString> soundOverrides;
	qint32 sendFilesWay = _sendFilesWay.serialize();
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
	qint32 mainMenuAccountsShown = _mainMenuAccountsShown.current() ? 1 : 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	qint32 floatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 floatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	qint32 thirdSectionInfoEnabled = 0;
	float64 dialogsWidthRatio = _dialogsWidthRatio.current();
	qint32 thirdColumnWidth = _thirdColumnWidth.current();
	qint32 thirdSectionExtendedBy = _thirdSectionExtendedBy;
	qint32 notifyFromAll = _notifyFromAll ? 1 : 0;
	qint32 nativeWindowFrame = _nativeWindowFrame.current() ? 1 : 0;
	qint32 systemDarkModeEnabled = _systemDarkModeEnabled.current() ? 1 : 0;
	qint32 ipRevealWarning = _ipRevealWarning ? 1 : 0;
	qint32 groupCallPushToTalk = _groupCallPushToTalk ? 1 : 0;
	QByteArray groupCallPushToTalkShortcut = _groupCallPushToTalkShortcut;
	qint64 groupCallPushToTalkDelay = _groupCallPushToTalkDelay;
	qint32 callAudioBackend = static_cast<qint32>(_callAudioBackend);

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
			>> callOutputDeviceId
			>> callInputDeviceId
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
			>> autoDownloadDictionaries
			>> mainMenuAccountsShown;
	}
	if (!stream.atEnd()) {
		auto dialogsWidthRatioInt = qint32();
		stream
			>> tabbedSelectorSectionEnabled
			>> floatPlayerColumn
			>> floatPlayerCorner
			>> thirdSectionInfoEnabled
			>> dialogsWidthRatioInt
			>> thirdColumnWidth
			>> thirdSectionExtendedBy
			>> notifyFromAll;
		dialogsWidthRatio = snap(dialogsWidthRatioInt / 1000000., 0., 1.);
	}
	if (!stream.atEnd()) {
		stream >> nativeWindowFrame;
	}
	if (!stream.atEnd()) {
		stream >> systemDarkModeEnabled;
	}
	if (!stream.atEnd()) {
		stream >> callVideoInputDeviceId;
	}
	if (!stream.atEnd()) {
		stream >> ipRevealWarning;
	}
	if (!stream.atEnd()) {
		stream
			>> groupCallPushToTalk
			>> groupCallPushToTalkShortcut
			>> groupCallPushToTalkDelay;
	}
	if (!stream.atEnd()) {
		stream >> callAudioBackend;
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
	_callOutputDeviceId = callOutputDeviceId;
	_callInputDeviceId = callInputDeviceId;
	_callVideoInputDeviceId = callVideoInputDeviceId;
	_callOutputVolume = callOutputVolume;
	_callInputVolume = callInputVolume;
	_callAudioDuckingEnabled = (callAudioDuckingEnabled == 1);
	_lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_soundOverrides = std::move(soundOverrides);
	_sendFilesWay = Ui::SendFilesWay::FromSerialized(sendFilesWay).value_or(_sendFilesWay);
	auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(sendSubmitWay);
	switch (uncheckedSendSubmitWay) {
	case Ui::InputSubmitSettings::Enter:
	case Ui::InputSubmitSettings::CtrlEnter: _sendSubmitWay = uncheckedSendSubmitWay; break;
	}
	_includeMutedCounter = (includeMutedCounter == 1);
	_countUnreadMessages = (countUnreadMessages == 1);
	_exeLaunchWarning = (exeLaunchWarning == 1);
	_ipRevealWarning = (ipRevealWarning == 1);
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
	_mainMenuAccountsShown = (mainMenuAccountsShown == 1);
	_tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
	auto uncheckedColumn = static_cast<Window::Column>(floatPlayerColumn);
	switch (uncheckedColumn) {
	case Window::Column::First:
	case Window::Column::Second:
	case Window::Column::Third: _floatPlayerColumn = uncheckedColumn; break;
	}
	auto uncheckedCorner = static_cast<RectPart>(floatPlayerCorner);
	switch (uncheckedCorner) {
	case RectPart::TopLeft:
	case RectPart::TopRight:
	case RectPart::BottomLeft:
	case RectPart::BottomRight: _floatPlayerCorner = uncheckedCorner; break;
	}
	_thirdSectionInfoEnabled = thirdSectionInfoEnabled;
	_dialogsWidthRatio = dialogsWidthRatio;
	_thirdColumnWidth = thirdColumnWidth;
	_thirdSectionExtendedBy = thirdSectionExtendedBy;
	if (_thirdSectionInfoEnabled) {
		_tabbedSelectorSectionEnabled = false;
	}
	_notifyFromAll = (notifyFromAll == 1);
	_nativeWindowFrame = (nativeWindowFrame == 1);
	_systemDarkModeEnabled = (systemDarkModeEnabled == 1);
	_groupCallPushToTalk = (groupCallPushToTalk == 1);
	_groupCallPushToTalkShortcut = groupCallPushToTalkShortcut;
	_groupCallPushToTalkDelay = groupCallPushToTalkDelay;
	auto uncheckedBackend = static_cast<Webrtc::Backend>(callAudioBackend);
	switch (uncheckedBackend) {
	case Webrtc::Backend::OpenAL:
	case Webrtc::Backend::ADM:
	case Webrtc::Backend::ADM2: _callAudioBackend = uncheckedBackend; break;
	}
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

void Settings::setTabbedSelectorSectionEnabled(bool enabled) {
	_tabbedSelectorSectionEnabled = enabled;
	if (enabled) {
		setThirdSectionInfoEnabled(false);
	}
	setTabbedReplacedWithInfo(false);
}

rpl::producer<bool> Settings::tabbedReplacedWithInfoValue() const {
	return _tabbedReplacedWithInfoValue.events_starting_with(
		tabbedReplacedWithInfo());
}

void Settings::setThirdSectionInfoEnabled(bool enabled) {
	if (_thirdSectionInfoEnabled != enabled) {
		_thirdSectionInfoEnabled = enabled;
		if (enabled) {
			setTabbedSelectorSectionEnabled(false);
		}
		setTabbedReplacedWithInfo(false);
		_thirdSectionInfoEnabledValue.fire_copy(enabled);
	}
}

rpl::producer<bool> Settings::thirdSectionInfoEnabledValue() const {
	return _thirdSectionInfoEnabledValue.events_starting_with(
		thirdSectionInfoEnabled());
}

void Settings::setTabbedReplacedWithInfo(bool enabled) {
	if (_tabbedReplacedWithInfo != enabled) {
		_tabbedReplacedWithInfo = enabled;
		_tabbedReplacedWithInfoValue.fire_copy(enabled);
	}
}

void Settings::setDialogsWidthRatio(float64 ratio) {
	_dialogsWidthRatio = ratio;
}

float64 Settings::dialogsWidthRatio() const {
	return _dialogsWidthRatio.current();
}

rpl::producer<float64> Settings::dialogsWidthRatioChanges() const {
	return _dialogsWidthRatio.changes();
}

void Settings::setThirdColumnWidth(int width) {
	_thirdColumnWidth = width;
}

int Settings::thirdColumnWidth() const {
	return _thirdColumnWidth.current();
}

rpl::producer<int> Settings::thirdColumnWidthChanges() const {
	return _thirdColumnWidth.changes();
}

void Settings::resetOnLastLogout() {
	_adaptiveForWide = true;
	_moderateModeEnabled = false;

	_songVolume = kDefaultVolume;
	_videoVolume = kDefaultVolume;

	_askDownloadPath = false;
	_downloadPath = QString();
	_downloadPathBookmark = QByteArray();

	_voiceMsgPlaybackDoubled = false;
	_soundNotify = true;
	_desktopNotify = true;
	_flashBounceNotify = true;
	_notifyView = dbinvShowPreview;
	//_nativeNotifications = false;
	//_notificationsCount = 3;
	//_notificationsCorner = ScreenCorner::BottomRight;
	_includeMutedCounter = true;
	_countUnreadMessages = true;
	_notifyAboutPinned = true;
	//_autoLock = 3600;

	//_callOutputDeviceId = u"default"_q;
	//_callInputDeviceId = u"default"_q;
	//_callVideoInputDeviceId = u"default"_q;
	//_callOutputVolume = 100;
	//_callInputVolume = 100;
	//_callAudioDuckingEnabled = true;

	_groupCallPushToTalk = false;
	_groupCallPushToTalkShortcut = QByteArray();
	_groupCallPushToTalkDelay = 20;

	//_themesAccentColors = Window::Theme::AccentColors();

	_lastSeenWarningSeen = false;
	_sendFilesWay = Ui::SendFilesWay();
	//_sendSubmitWay = Ui::InputSubmitSettings::Enter;
	_soundOverrides = {};

	_exeLaunchWarning = true;
	_ipRevealWarning = true;
	_loopAnimatedStickers = true;
	_largeEmoji = true;
	_replaceEmoji = true;
	_suggestEmoji = true;
	_suggestStickersByEmoji = true;
	_spellcheckerEnabled = true;
	_videoPlaybackSpeed = 1.;
	//_videoPipGeometry = QByteArray();
	_dictionariesEnabled = std::vector<int>();
	_autoDownloadDictionaries = true;
	_mainMenuAccountsShown = true;
	_tabbedSelectorSectionEnabled = false; // per-window
	_floatPlayerColumn = Window::Column::Second; // per-window
	_floatPlayerCorner = RectPart::TopRight; // per-window
	_thirdSectionInfoEnabled = true; // per-window
	_thirdSectionExtendedBy = -1; // per-window
	_dialogsWidthRatio = DefaultDialogsWidthRatio(); // per-window
	_thirdColumnWidth = kDefaultThirdColumnWidth; // p-w
	_notifyFromAll = true;
	_tabbedReplacedWithInfo = false; // per-window
	_systemDarkModeEnabled = false;
}

bool Settings::ThirdColumnByDefault() {
	return Platform::IsMacStoreBuild();
}

float64 Settings::DefaultDialogsWidthRatio() {
	return ThirdColumnByDefault()
		? kDefaultBigDialogsWidthRatio
		: kDefaultDialogsWidthRatio;
}

} // namespace Core
