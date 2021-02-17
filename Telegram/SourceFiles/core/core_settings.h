/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/core_settings_proxy.h"
#include "window/themes/window_themes_embedded.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "platform/platform_notifications_manager.h"
#include "base/flags.h"
#include "emoji.h"

enum class RectPart;

namespace Ui {
enum class InputSubmitSettings;
} // namespace Ui

namespace Window {
enum class Column;
} // namespace Window

namespace Webrtc {
enum class Backend;
} // namespace Webrtc

namespace Calls::Group {
enum class StickedTooltip;
} // namespace Calls::Group

namespace Core {

struct WindowPosition {
	WindowPosition() = default;

	int32 moncrc = 0;
	int maximized = 0;
	int scale = 0;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

class Settings final {
public:
	enum class ScreenCorner {
		TopLeft = 0,
		TopRight = 1,
		BottomRight = 2,
		BottomLeft = 3,
	};
	enum class NotifyView {
		ShowPreview = 0,
		ShowName = 1,
		ShowNothing = 2,
	};
	enum class WorkMode {
		WindowAndTray = 0,
		TrayOnly = 1,
		WindowOnly = 2,
	};

	static constexpr auto kDefaultVolume = 0.9;

	Settings();

	[[nodiscard]] rpl::producer<> saveDelayedRequests() const {
		return _saveDelayed.events();
	}

	[[nodiscard]] SettingsProxy &proxy() {
		return _proxy;
	}

	[[nodiscard]] static bool IsLeftCorner(ScreenCorner corner) {
		return (corner == ScreenCorner::TopLeft)
			|| (corner == ScreenCorner::BottomLeft);
	}
	[[nodiscard]] static bool IsTopCorner(ScreenCorner corner) {
		return (corner == ScreenCorner::TopLeft)
			|| (corner == ScreenCorner::TopRight);
	}

	[[nodiscard]] QByteArray serialize() const;
	void addFromSerialized(const QByteArray &serialized);

	[[nodiscard]] bool adaptiveForWide() const {
		return _adaptiveForWide.current();
	}
	[[nodiscard]] rpl::producer<bool> adaptiveForWideValue() const {
		return _adaptiveForWide.value();
	}
	[[nodiscard]] rpl::producer<bool> adaptiveForWideChanges() const {
		return _adaptiveForWide.changes();
	}
	void setAdaptiveForWide(bool value) {
		_adaptiveForWide = value;
	}
	[[nodiscard]] bool moderateModeEnabled() const {
		return _moderateModeEnabled;
	}
	void setModerateModeEnabled(bool value) {
		_moderateModeEnabled = value;
	}
	[[nodiscard]] float64 songVolume() const {
		return _songVolume.current();
	}
	[[nodiscard]] rpl::producer<float64> songVolumeChanges() const {
		return _songVolume.changes();
	}
	void setSongVolume(float64 value) {
		_songVolume = value;
	}
	[[nodiscard]] float64 videoVolume() const {
		return _videoVolume.current();
	}
	[[nodiscard]] rpl::producer<float64> videoVolumeChanges() const {
		return _videoVolume.changes();
	}
	void setVideoVolume(float64 value) {
		_videoVolume = value;
	}
	[[nodiscard]] bool askDownloadPath() const {
		return _askDownloadPath;
	}
	void setAskDownloadPath(bool value) {
		_askDownloadPath = value;
	}
	[[nodiscard]] QString downloadPath() const {
		return _downloadPath.current();
	}
	[[nodiscard]] rpl::producer<QString> downloadPathValue() const {
		return _downloadPath.value();
	}
	void setDownloadPath(const QString &value) {
		_downloadPath = value;
	}
	[[nodiscard]] QByteArray downloadPathBookmark() const {
		return _downloadPathBookmark;
	}
	void setDownloadPathBookmark(const QByteArray &value) {
		_downloadPathBookmark = value;
	}
	[[nodiscard]] bool voiceMsgPlaybackDoubled() const {
		return _voiceMsgPlaybackDoubled;
	}
	void setVoiceMsgPlaybackDoubled(bool value) {
		_voiceMsgPlaybackDoubled = value;
	}
	[[nodiscard]] bool soundNotify() const {
		return _soundNotify;
	}
	void setSoundNotify(bool value) {
		_soundNotify = value;
	}
	[[nodiscard]] bool desktopNotify() const {
		return _desktopNotify;
	}
	void setDesktopNotify(bool value) {
		_desktopNotify = value;
	}
	[[nodiscard]] bool flashBounceNotify() const {
		return _flashBounceNotify;
	}
	void setFlashBounceNotify(bool value) {
		_flashBounceNotify = value;
	}
	[[nodiscard]] NotifyView notifyView() const {
		return _notifyView;
	}
	void setNotifyView(NotifyView value) {
		_notifyView = value;
	}
	[[nodiscard]] bool nativeNotifications() const {
		return _nativeNotifications.value_or(Platform::Notifications::ByDefault());
	}
	void setNativeNotifications(bool value) {
		_nativeNotifications = (value == Platform::Notifications::ByDefault())
			? std::nullopt
			: std::make_optional(value);
	}
	[[nodiscard]] int notificationsCount() const {
		return _notificationsCount;
	}
	void setNotificationsCount(int value) {
		_notificationsCount = value;
	}
	[[nodiscard]] ScreenCorner notificationsCorner() const {
		return _notificationsCorner;
	}
	void setNotificationsCorner(ScreenCorner corner) {
		_notificationsCorner = corner;
	}
	[[nodiscard]] bool includeMutedCounter() const {
		return _includeMutedCounter;
	}
	void setIncludeMutedCounter(bool value) {
		_includeMutedCounter = value;
	}
	[[nodiscard]] bool countUnreadMessages() const {
		return _countUnreadMessages;
	}
	void setCountUnreadMessages(bool value) {
		_countUnreadMessages = value;
	}
	void setNotifyAboutPinned(bool notify) {
		_notifyAboutPinned = notify;
	}
	[[nodiscard]] bool notifyAboutPinned() const {
		return _notifyAboutPinned.current();
	}
	[[nodiscard]] rpl::producer<bool> notifyAboutPinnedChanges() const {
		return _notifyAboutPinned.changes();
	}
	[[nodiscard]] int autoLock() const {
		return _autoLock;
	}
	void setAutoLock(int value) {
		_autoLock = value;
	}
	[[nodiscard]] QString callOutputDeviceId() const {
		return _callOutputDeviceId.isEmpty()
			? u"default"_q
			: _callOutputDeviceId;
	}
	void setCallOutputDeviceId(const QString &value) {
		_callOutputDeviceId = value;
	}
	[[nodiscard]] QString callInputDeviceId() const {
		return _callInputDeviceId.isEmpty()
			? u"default"_q
			: _callInputDeviceId;
	}
	void setCallInputDeviceId(const QString &value) {
		_callInputDeviceId = value;
	}
	[[nodiscard]] QString callVideoInputDeviceId() const {
		return _callVideoInputDeviceId.isEmpty()
			? u"default"_q
			: _callVideoInputDeviceId;
	}
	void setCallVideoInputDeviceId(const QString &value) {
		_callVideoInputDeviceId = value;
	}
	[[nodiscard]] int callOutputVolume() const {
		return _callOutputVolume;
	}
	void setCallOutputVolume(int value) {
		_callOutputVolume = value;
	}
	[[nodiscard]] int callInputVolume() const {
		return _callInputVolume;
	}
	void setCallInputVolume(int value) {
		_callInputVolume = value;
	}
	[[nodiscard]] bool callAudioDuckingEnabled() const {
		return _callAudioDuckingEnabled;
	}
	void setCallAudioDuckingEnabled(bool value) {
		_callAudioDuckingEnabled = value;
	}
	[[nodiscard]] Webrtc::Backend callAudioBackend() const;
	void setDisableCalls(bool value) {
		_disableCalls = value;
	}
	[[nodiscard]] bool disableCalls() const {
		return _disableCalls;
	}
	[[nodiscard]] bool groupCallPushToTalk() const {
		return _groupCallPushToTalk;
	}
	void setGroupCallPushToTalk(bool value) {
		_groupCallPushToTalk = value;
	}
	[[nodiscard]] QByteArray groupCallPushToTalkShortcut() const {
		return _groupCallPushToTalkShortcut;
	}
	void setGroupCallPushToTalkShortcut(const QByteArray &serialized) {
		_groupCallPushToTalkShortcut = serialized;
	}
	[[nodiscard]] crl::time groupCallPushToTalkDelay() const {
		return _groupCallPushToTalkDelay;
	}
	void setGroupCallPushToTalkDelay(crl::time delay) {
		_groupCallPushToTalkDelay = delay;
	}
	[[nodiscard]] bool groupCallNoiseSuppression() const {
		return _groupCallNoiseSuppression;
	}
	void setGroupCallNoiseSuppression(bool value) {
		_groupCallNoiseSuppression = value;
	}
	[[nodiscard]] Window::Theme::AccentColors &themesAccentColors() {
		return _themesAccentColors;
	}
	void setThemesAccentColors(Window::Theme::AccentColors &&colors) {
		_themesAccentColors = std::move(colors);
	}
	void setLastSeenWarningSeen(bool lastSeenWarningSeen) {
		_lastSeenWarningSeen = lastSeenWarningSeen;
	}
	[[nodiscard]] bool lastSeenWarningSeen() const {
		return _lastSeenWarningSeen;
	}
	void setSendFilesWay(Ui::SendFilesWay way) {
		_sendFilesWay = way;
	}
	[[nodiscard]] Ui::SendFilesWay sendFilesWay() const {
		return _sendFilesWay;
	}
	void setSendSubmitWay(Ui::InputSubmitSettings value) {
		_sendSubmitWay = value;
	}
	[[nodiscard]] Ui::InputSubmitSettings sendSubmitWay() const {
		return _sendSubmitWay;
	}
	void setSoundOverride(const QString &key, const QString &path) {
		_soundOverrides.emplace(key, path);
	}
	void clearSoundOverrides() {
		_soundOverrides.clear();
	}
	[[nodiscard]] QString getSoundPath(const QString &key) const;

	[[nodiscard]] bool exeLaunchWarning() const {
		return _exeLaunchWarning;
	}
	void setExeLaunchWarning(bool warning) {
		_exeLaunchWarning = warning;
	}
	[[nodiscard]] bool ipRevealWarning() const {
		return _ipRevealWarning;
	}
	void setIpRevealWarning(bool warning) {
		_ipRevealWarning = warning;
	}
	[[nodiscard]] bool loopAnimatedStickers() const {
		return _loopAnimatedStickers;
	}
	void setLoopAnimatedStickers(bool value) {
		_loopAnimatedStickers = value;
	}
	void setLargeEmoji(bool value) {
		_largeEmoji = value;
	}
	[[nodiscard]] bool largeEmoji() const {
		return _largeEmoji.current();
	}
	[[nodiscard]] rpl::producer<bool> largeEmojiValue() const {
		return _largeEmoji.value();
	}
	[[nodiscard]] rpl::producer<bool> largeEmojiChanges() const {
		return _largeEmoji.changes();
	}
	void setReplaceEmoji(bool value) {
		_replaceEmoji = value;
	}
	[[nodiscard]] bool replaceEmoji() const {
		return _replaceEmoji.current();
	}
	[[nodiscard]] rpl::producer<bool> replaceEmojiValue() const {
		return _replaceEmoji.value();
	}
	[[nodiscard]] rpl::producer<bool> replaceEmojiChanges() const {
		return _replaceEmoji.changes();
	}
	[[nodiscard]] bool suggestEmoji() const {
		return _suggestEmoji;
	}
	void setSuggestEmoji(bool value) {
		_suggestEmoji = value;
	}
	[[nodiscard]] bool suggestStickersByEmoji() const {
		return _suggestStickersByEmoji;
	}
	void setSuggestStickersByEmoji(bool value) {
		_suggestStickersByEmoji = value;
	}

	void setSpellcheckerEnabled(bool value) {
		_spellcheckerEnabled = value;
	}
	bool spellcheckerEnabled() const {
		return _spellcheckerEnabled.current();
	}
	rpl::producer<bool> spellcheckerEnabledValue() const {
		return _spellcheckerEnabled.value();
	}
	rpl::producer<bool> spellcheckerEnabledChanges() const {
		return _spellcheckerEnabled.changes();
	}

	void setDictionariesEnabled(std::vector<int> dictionaries) {
		_dictionariesEnabled = std::move(dictionaries);
	}

	std::vector<int> dictionariesEnabled() const {
		return _dictionariesEnabled.current();
	}

	rpl::producer<std::vector<int>> dictionariesEnabledChanges() const {
		return _dictionariesEnabled.changes();
	}

	void setAutoDownloadDictionaries(bool value) {
		_autoDownloadDictionaries = value;
	}
	bool autoDownloadDictionaries() const {
		return _autoDownloadDictionaries.current();
	}
	rpl::producer<bool> autoDownloadDictionariesValue() const {
		return _autoDownloadDictionaries.value();
	}
	rpl::producer<bool> autoDownloadDictionariesChanges() const {
		return _autoDownloadDictionaries.changes();
	}

	[[nodiscard]] float64 videoPlaybackSpeed() const {
		return _videoPlaybackSpeed.current();
	}
	void setVideoPlaybackSpeed(float64 speed) {
		_videoPlaybackSpeed = speed;
	}
	[[nodiscard]] QByteArray videoPipGeometry() const {
		return _videoPipGeometry;
	}
	void setVideoPipGeometry(QByteArray geometry) {
		_videoPipGeometry = geometry;
	}

	[[nodiscard]] QByteArray photoEditorBrush() const {
		return _photoEditorBrush;
	}
	void setPhotoEditorBrush(QByteArray brush) {
		_photoEditorBrush = brush;
	}

	[[nodiscard]] float64 rememberedSongVolume() const {
		return _rememberedSongVolume;
	}
	void setRememberedSongVolume(float64 value) {
		_rememberedSongVolume = value;
	}
	[[nodiscard]] bool rememberedSoundNotifyFromTray() const {
		return _rememberedSoundNotifyFromTray;
	}
	void setRememberedSoundNotifyFromTray(bool value) {
		_rememberedSoundNotifyFromTray = value;
	}
	[[nodiscard]] bool rememberedFlashBounceNotifyFromTray() const {
		return _rememberedFlashBounceNotifyFromTray;
	}
	void setRememberedFlashBounceNotifyFromTray(bool value) {
		_rememberedFlashBounceNotifyFromTray = value;
	}
	[[nodiscard]] bool mainMenuAccountsShown() const {
		return _mainMenuAccountsShown.current();
	}
	[[nodiscard]] rpl::producer<bool> mainMenuAccountsShownValue() const {
		return _mainMenuAccountsShown.value();
	}
	void setMainMenuAccountsShown(bool value) {
		_mainMenuAccountsShown = value;
	}
	[[nodiscard]] bool tabbedSelectorSectionEnabled() const {
		return _tabbedSelectorSectionEnabled;
	}
	void setTabbedSelectorSectionEnabled(bool enabled);
	[[nodiscard]] bool thirdSectionInfoEnabled() const {
		return _thirdSectionInfoEnabled;
	}
	void setThirdSectionInfoEnabled(bool enabled);
	[[nodiscard]] rpl::producer<bool> thirdSectionInfoEnabledValue() const;
	[[nodiscard]] int thirdSectionExtendedBy() const {
		return _thirdSectionExtendedBy;
	}
	void setThirdSectionExtendedBy(int savedValue) {
		_thirdSectionExtendedBy = savedValue;
	}
	[[nodiscard]] bool tabbedReplacedWithInfo() const {
		return _tabbedReplacedWithInfo;
	}
	void setTabbedReplacedWithInfo(bool enabled);
	[[nodiscard]] rpl::producer<bool> tabbedReplacedWithInfoValue() const;
	void setFloatPlayerColumn(Window::Column column) {
		_floatPlayerColumn = column;
	}
	[[nodiscard]] Window::Column floatPlayerColumn() const {
		return _floatPlayerColumn;
	}
	void setFloatPlayerCorner(RectPart corner) {
		_floatPlayerCorner = corner;
	}
	[[nodiscard]] RectPart floatPlayerCorner() const {
		return _floatPlayerCorner;
	}
	void setDialogsWidthRatio(float64 ratio);
	[[nodiscard]] float64 dialogsWidthRatio() const;
	[[nodiscard]] rpl::producer<float64> dialogsWidthRatioChanges() const;
	void setThirdColumnWidth(int width);
	[[nodiscard]] int thirdColumnWidth() const;
	[[nodiscard]] rpl::producer<int> thirdColumnWidthChanges() const;
	void setNotifyFromAll(bool value) {
		_notifyFromAll = value;
	}
	[[nodiscard]] bool notifyFromAll() const {
		return _notifyFromAll;
	}
	void setNativeWindowFrame(bool value) {
		_nativeWindowFrame = value;
	}
	[[nodiscard]] bool nativeWindowFrame() const {
		return _nativeWindowFrame.current();
	}
	[[nodiscard]] rpl::producer<bool> nativeWindowFrameChanges() const {
		return _nativeWindowFrame.changes();
	}
	void setSystemDarkMode(std::optional<bool> value) {
		_systemDarkMode = value;
	}
	[[nodiscard]] std::optional<bool> systemDarkMode() const {
		return _systemDarkMode.current();
	}
	[[nodiscard]] rpl::producer<std::optional<bool>> systemDarkModeValue() const {
		return _systemDarkMode.value();
	}
	[[nodiscard]] rpl::producer<std::optional<bool>> systemDarkModeChanges() const {
		return _systemDarkMode.changes();
	}
	void setSystemDarkModeEnabled(bool value) {
		_systemDarkModeEnabled = value;
	}
	[[nodiscard]] bool systemDarkModeEnabled() const {
		return _systemDarkModeEnabled.current();
	}
	[[nodiscard]] rpl::producer<bool> systemDarkModeEnabledValue() const {
		return _systemDarkModeEnabled.value();
	}
	[[nodiscard]] rpl::producer<bool> systemDarkModeEnabledChanges() const {
		return _systemDarkModeEnabled.changes();
	}
	[[nodiscard]] const WindowPosition &windowPosition() const {
		return _windowPosition;
	}
	void setWindowPosition(const WindowPosition &position) {
		_windowPosition = position;
	}
	void setWorkMode(WorkMode value) {
		_workMode = value;
	}
	[[nodiscard]] WorkMode workMode() const {
		return _workMode.current();
	}
	[[nodiscard]] rpl::producer<WorkMode> workModeValue() const {
		return _workMode.value();
	}
	[[nodiscard]] rpl::producer<WorkMode> workModeChanges() const {
		return _workMode.changes();
	}

	struct RecentEmoji {
		EmojiPtr emoji = nullptr;
		ushort rating = 0;
	};
	[[nodiscard]] const std::vector<RecentEmoji> &recentEmoji() const;
	[[nodiscard]] EmojiPack recentEmojiSection() const;
	void incrementRecentEmoji(EmojiPtr emoji);
	void setLegacyRecentEmojiPreload(QVector<QPair<QString, ushort>> data);
	[[nodiscard]] rpl::producer<> recentEmojiUpdated() const {
		return _recentEmojiUpdated.events();
	}

	[[nodiscard]] const base::flat_map<QString, uint8> &emojiVariants() const {
		return _emojiVariants;
	}
	void saveEmojiVariant(EmojiPtr emoji);
	void setLegacyEmojiVariants(QMap<QString, int> data);

	[[nodiscard]] bool disableOpenGL() const {
		return _disableOpenGL;
	}
	void setDisableOpenGL(bool value) {
		_disableOpenGL = value;
	}

	[[nodiscard]] base::flags<Calls::Group::StickedTooltip> hiddenGroupCallTooltips() const {
		return _hiddenGroupCallTooltips;
	}
	void setHiddenGroupCallTooltip(Calls::Group::StickedTooltip value) {
		_hiddenGroupCallTooltips |= value;
	}

	[[nodiscard]] static bool ThirdColumnByDefault();
	[[nodiscard]] static float64 DefaultDialogsWidthRatio();
	[[nodiscard]] static qint32 SerializePlaybackSpeed(float64 speed) {
		return int(std::round(std::clamp(speed, 0.5, 2.0) * 100));
	}
	[[nodiscard]] static float64 DeserializePlaybackSpeed(qint32 speed) {
		if (speed < 10) {
			// The old values in settings.
			return (std::clamp(speed, 0, 6) + 2) / 4.;
		} else {
			return std::clamp(speed, 50, 200) / 100.;
		}
	}

	void resetOnLastLogout();

private:
	void resolveRecentEmoji() const;

	static constexpr auto kDefaultThirdColumnWidth = 0;
	static constexpr auto kDefaultDialogsWidthRatio = 5. / 14;
	static constexpr auto kDefaultBigDialogsWidthRatio = 0.275;

	struct RecentEmojiId {
		QString emoji;
		ushort rating = 0;
	};

	SettingsProxy _proxy;

	rpl::variable<bool> _adaptiveForWide = true;
	bool _moderateModeEnabled = false;
	rpl::variable<float64> _songVolume = kDefaultVolume;
	rpl::variable<float64> _videoVolume = kDefaultVolume;
	bool _askDownloadPath = false;
	rpl::variable<QString> _downloadPath;
	QByteArray _downloadPathBookmark;
	bool _voiceMsgPlaybackDoubled = false;
	bool _soundNotify = true;
	bool _desktopNotify = true;
	bool _flashBounceNotify = true;
	NotifyView _notifyView = NotifyView::ShowPreview;
	std::optional<bool> _nativeNotifications;
	int _notificationsCount = 3;
	ScreenCorner _notificationsCorner = ScreenCorner::BottomRight;
	bool _includeMutedCounter = true;
	bool _countUnreadMessages = true;
	rpl::variable<bool> _notifyAboutPinned = true;
	int _autoLock = 3600;
	QString _callOutputDeviceId = u"default"_q;
	QString _callInputDeviceId = u"default"_q;
	QString _callVideoInputDeviceId = u"default"_q;
	int _callOutputVolume = 100;
	int _callInputVolume = 100;
	bool _callAudioDuckingEnabled = true;
	bool _disableCalls = false;
	bool _groupCallPushToTalk = false;
	bool _groupCallNoiseSuppression = true;
	QByteArray _groupCallPushToTalkShortcut;
	crl::time _groupCallPushToTalkDelay = 20;
	Window::Theme::AccentColors _themesAccentColors;
	bool _lastSeenWarningSeen = false;
	Ui::SendFilesWay _sendFilesWay = Ui::SendFilesWay();
	Ui::InputSubmitSettings _sendSubmitWay = Ui::InputSubmitSettings();
	base::flat_map<QString, QString> _soundOverrides;
	bool _exeLaunchWarning = true;
	bool _ipRevealWarning = true;
	bool _loopAnimatedStickers = true;
	rpl::variable<bool> _largeEmoji = true;
	rpl::variable<bool> _replaceEmoji = true;
	bool _suggestEmoji = true;
	bool _suggestStickersByEmoji = true;
	rpl::variable<bool> _spellcheckerEnabled = true;
	rpl::variable<float64> _videoPlaybackSpeed = 1.;
	QByteArray _videoPipGeometry;
	rpl::variable<std::vector<int>> _dictionariesEnabled;
	rpl::variable<bool> _autoDownloadDictionaries = true;
	rpl::variable<bool> _mainMenuAccountsShown = true;
	mutable std::vector<RecentEmojiId> _recentEmojiPreload;
	mutable std::vector<RecentEmoji> _recentEmoji;
	base::flat_map<QString, uint8> _emojiVariants;
	rpl::event_stream<> _recentEmojiUpdated;
	bool _tabbedSelectorSectionEnabled = false; // per-window
	Window::Column _floatPlayerColumn = Window::Column(); // per-window
	RectPart _floatPlayerCorner = RectPart(); // per-window
	bool _thirdSectionInfoEnabled = true; // per-window
	rpl::event_stream<bool> _thirdSectionInfoEnabledValue; // per-window
	int _thirdSectionExtendedBy = -1; // per-window
	rpl::variable<float64> _dialogsWidthRatio; // per-window
	rpl::variable<int> _thirdColumnWidth = kDefaultThirdColumnWidth; // p-w
	bool _notifyFromAll = true;
	rpl::variable<bool> _nativeWindowFrame = false;
	rpl::variable<std::optional<bool>> _systemDarkMode = std::nullopt;
	rpl::variable<bool> _systemDarkModeEnabled = false;
	WindowPosition _windowPosition; // per-window
	bool _disableOpenGL = false;
	rpl::variable<WorkMode> _workMode = WorkMode::WindowAndTray;
	base::flags<Calls::Group::StickedTooltip> _hiddenGroupCallTooltips;

	bool _tabbedReplacedWithInfo = false; // per-window
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue; // per-window

	rpl::event_stream<> _saveDelayed;
	float64 _rememberedSongVolume = kDefaultVolume;
	bool _rememberedSoundNotifyFromTray = false;
	bool _rememberedFlashBounceNotifyFromTray = false;

	QByteArray _photoEditorBrush;

};

} // namespace Core

