/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/core_settings_proxy.h"
#include "media/media_common.h"
#include "window/themes/window_themes_embedded.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "base/flags.h"
#include "emoji.h"

enum class RectPart;
struct LanguageId;

namespace Ui {
enum class InputSubmitSettings;
} // namespace Ui

namespace HistoryView {
enum class DoubleClickQuickAction;
} // namespace HistoryView

namespace Window {
enum class Column;
} // namespace Window

namespace Calls::Group {
enum class StickedTooltip;
} // namespace Calls::Group

namespace Core {

struct WindowPosition {
	int32 moncrc = 0;
	int maximized = 0;
	int scale = 0;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;

	friend inline constexpr auto operator<=>(
		WindowPosition,
		WindowPosition) = default;

	[[nodiscard]] QRect rect() const {
		return QRect(x, y, w, h);
	}
};

[[nodiscard]] WindowPosition AdjustToScale(
	WindowPosition position,
	const QString &name);

struct WindowTitleContent {
	bool hideChatName : 1 = false;
	bool hideAccountName : 1 = false;
	bool hideTotalUnread : 1 = false;

	friend inline constexpr auto operator<=>(
		WindowTitleContent,
		WindowTitleContent) = default;
};

constexpr auto kRecentEmojiLimit = 54;

struct RecentEmojiDocument {
	DocumentId id = 0;
	bool test = false;

	friend inline auto operator<=>(
		RecentEmojiDocument,
		RecentEmojiDocument) = default;
};

struct RecentEmojiId {
	std::variant<EmojiPtr, RecentEmojiDocument> data;

	friend inline bool operator==(
		RecentEmojiId,
		RecentEmojiId) = default;
};

struct RecentEmoji {
	RecentEmojiId id;
	ushort rating = 0;
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
	~Settings();

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

	[[nodiscard]] bool nativeNotifications() const;
	void setNativeNotifications(bool value);

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

	[[nodiscard]] QString playbackDeviceId() const {
		return _playbackDeviceId.current();
	}
	[[nodiscard]] rpl::producer<QString> playbackDeviceIdChanges() const {
		return _playbackDeviceId.changes();
	}
	[[nodiscard]] rpl::producer<QString> playbackDeviceIdValue() const {
		return _playbackDeviceId.value();
	}
	void setPlaybackDeviceId(const QString &value) {
		_playbackDeviceId = value;
	}
	[[nodiscard]] QString captureDeviceId() const {
		return _captureDeviceId.current();
	}
	[[nodiscard]] rpl::producer<QString> captureDeviceIdChanges() const {
		return _captureDeviceId.changes();
	}
	[[nodiscard]] rpl::producer<QString> captureDeviceIdValue() const {
		return _captureDeviceId.value();
	}
	void setCaptureDeviceId(const QString &value) {
		_captureDeviceId = value;
	}
	[[nodiscard]] QString cameraDeviceId() const {
		return _cameraDeviceId.current();
	}
	[[nodiscard]] rpl::producer<QString> cameraDeviceIdChanges() const {
		return _cameraDeviceId.changes();
	}
	[[nodiscard]] rpl::producer<QString> cameraDeviceIdValue() const {
		return _cameraDeviceId.value();
	}
	void setCameraDeviceId(const QString &value) {
		_cameraDeviceId = value;
	}
	[[nodiscard]] QString callPlaybackDeviceId() const {
		return _callPlaybackDeviceId.current();
	}
	[[nodiscard]] rpl::producer<QString> callPlaybackDeviceIdChanges() const {
		return _callPlaybackDeviceId.changes();
	}
	[[nodiscard]] rpl::producer<QString> callPlaybackDeviceIdValue() const {
		return _callPlaybackDeviceId.value();
	}
	void setCallPlaybackDeviceId(const QString &value) {
		_callPlaybackDeviceId = value;
	}
	[[nodiscard]] QString callCaptureDeviceId() const {
		return _callCaptureDeviceId.current();
	}
	[[nodiscard]] rpl::producer<QString> callCaptureDeviceIdChanges() const {
		return _callCaptureDeviceId.changes();
	}
	[[nodiscard]] rpl::producer<QString> callCaptureDeviceIdValue() const {
		return _callCaptureDeviceId.value();
	}
	void setCallCaptureDeviceId(const QString &value) {
		_callCaptureDeviceId = value;
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
	[[nodiscard]] bool disableCallsLegacy() const {
		return _disableCallsLegacy;
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

	[[nodiscard]] auto noWarningExtensions() const
	-> const base::flat_set<QString> & {
		return _noWarningExtensions;
	}
	void setNoWarningExtensions(base::flat_set<QString> extensions) {
		_noWarningExtensions = std::move(extensions);
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
	[[nodiscard]] bool suggestAnimatedEmoji() const {
		return _suggestAnimatedEmoji;
	}
	void setSuggestAnimatedEmoji(bool value) {
		_suggestAnimatedEmoji = value;
	}
	void setCornerReaction(bool value) {
		_cornerReaction = value;
	}
	[[nodiscard]] bool cornerReaction() const {
		return _cornerReaction.current();
	}
	[[nodiscard]] rpl::producer<bool> cornerReactionValue() const {
		return _cornerReaction.value();
	}
	[[nodiscard]] rpl::producer<bool> cornerReactionChanges() const {
		return _cornerReaction.changes();
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

	[[nodiscard]] float64 videoPlaybackSpeed(
			bool lastNonDefault = false) const {
		return (_videoPlaybackSpeed.enabled || lastNonDefault)
			? _videoPlaybackSpeed.value
			: 1.;
	}
	void setVideoPlaybackSpeed(float64 speed) {
		if ((_videoPlaybackSpeed.enabled = !Media::EqualSpeeds(speed, 1.))) {
			_videoPlaybackSpeed.value = speed;
		}
	}
	[[nodiscard]] float64 voicePlaybackSpeed(
			bool lastNonDefault = false) const {
		return (_voicePlaybackSpeed.enabled || lastNonDefault)
			? _voicePlaybackSpeed.value
			: 1.;
	}
	void setVoicePlaybackSpeed(float64 speed) {
		if ((_voicePlaybackSpeed.enabled = !Media::EqualSpeeds(speed, 1.0))) {
			_voicePlaybackSpeed.value = speed;
		}
	}

	// For legacy values read-write outside of Settings.
	[[nodiscard]] qint32 videoPlaybackSpeedSerialized() const {
		return SerializePlaybackSpeed(_videoPlaybackSpeed);
	}
	void setVideoPlaybackSpeedSerialized(qint32 value) {
		_videoPlaybackSpeed = DeserializePlaybackSpeed(value);
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

	void updateDialogsWidthRatio(float64 ratio, bool nochat);
	[[nodiscard]] float64 dialogsWidthRatio(bool nochat) const;

	[[nodiscard]] float64 dialogsWithChatWidthRatio() const;
	[[nodiscard]] rpl::producer<float64> dialogsWithChatWidthRatioChanges() const;
	[[nodiscard]] float64 dialogsNoChatWidthRatio() const;
	[[nodiscard]] rpl::producer<float64> dialogsNoChatWidthRatioChanges() const;

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
	[[nodiscard]] WindowTitleContent windowTitleContent() const {
		return _windowTitleContent.current();
	}
	[[nodiscard]] rpl::producer<WindowTitleContent> windowTitleContentChanges() const {
		return _windowTitleContent.changes();
	}
	void setWindowTitleContent(WindowTitleContent content) {
		_windowTitleContent = content;
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

	[[nodiscard]] const std::vector<RecentEmoji> &recentEmoji() const;
	void incrementRecentEmoji(RecentEmojiId id);
	void hideRecentEmoji(RecentEmojiId id);
	void resetRecentEmoji();
	void setLegacyRecentEmojiPreload(QVector<QPair<QString, ushort>> data);
	[[nodiscard]] rpl::producer<> recentEmojiUpdated() const {
		return _recentEmojiUpdated.events();
	}

	[[nodiscard]] const base::flat_map<QString, uint8> &emojiVariants() const {
		return _emojiVariants;
	}
	[[nodiscard]] EmojiPtr lookupEmojiVariant(EmojiPtr emoji) const;
	[[nodiscard]] bool hasChosenEmojiVariant(EmojiPtr emoji) const;
	void saveEmojiVariant(EmojiPtr emoji);
	void saveAllEmojiVariants(EmojiPtr emoji);
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

	void setCloseToTaskbar(bool value) {
		_closeToTaskbar = value;
	}
	[[nodiscard]] bool closeToTaskbar() const {
		return _closeToTaskbar.current();
	}
	[[nodiscard]] rpl::producer<bool> closeToTaskbarValue() const {
		return _closeToTaskbar.value();
	}
	[[nodiscard]] rpl::producer<bool> closeToTaskbarChanges() const {
		return _closeToTaskbar.changes();
	}
	void setTrayIconMonochrome(bool value) {
		_trayIconMonochrome = value;
	}
	[[nodiscard]] bool trayIconMonochrome() const {
		return _trayIconMonochrome.current();
	}
	[[nodiscard]] rpl::producer<bool> trayIconMonochromeChanges() const {
		return _trayIconMonochrome.changes();
	}

	void setCustomDeviceModel(const QString &model) {
		_customDeviceModel = model;
	}
	[[nodiscard]] QString customDeviceModel() const {
		return _customDeviceModel.current();
	}
	[[nodiscard]] rpl::producer<QString> customDeviceModelChanges() const {
		return _customDeviceModel.changes();
	}
	[[nodiscard]] rpl::producer<QString> customDeviceModelValue() const {
		return _customDeviceModel.value();
	}
	[[nodiscard]] QString deviceModel() const;
	[[nodiscard]] rpl::producer<QString> deviceModelChanges() const;
	[[nodiscard]] rpl::producer<QString> deviceModelValue() const;

	void setPlayerRepeatMode(Media::RepeatMode mode) {
		_playerRepeatMode = mode;
	}
	[[nodiscard]] Media::RepeatMode playerRepeatMode() const {
		return _playerRepeatMode.current();
	}
	[[nodiscard]] rpl::producer<Media::RepeatMode> playerRepeatModeValue() const {
		return _playerRepeatMode.value();
	}
	[[nodiscard]] rpl::producer<Media::RepeatMode> playerRepeatModeChanges() const {
		return _playerRepeatMode.changes();
	}
	void setPlayerOrderMode(Media::OrderMode mode) {
		_playerOrderMode = mode;
	}
	[[nodiscard]] Media::OrderMode playerOrderMode() const {
		return _playerOrderMode.current();
	}
	[[nodiscard]] rpl::producer<Media::OrderMode> playerOrderModeValue() const {
		return _playerOrderMode.value();
	}
	[[nodiscard]] rpl::producer<Media::OrderMode> playerOrderModeChanges() const {
		return _playerOrderMode.changes();
	}
	[[nodiscard]] std::vector<uint64> accountsOrder() const {
		return _accountsOrder;
	}
	void setAccountsOrder(const std::vector<uint64> &order) {
		_accountsOrder = order;
	}

	[[nodiscard]] bool hardwareAcceleratedVideo() const {
		return _hardwareAcceleratedVideo;
	}
	void setHardwareAcceleratedVideo(bool value) {
		_hardwareAcceleratedVideo = value;
	}

	void setMacWarnBeforeQuit(bool value) {
		_macWarnBeforeQuit = value;
	}
	[[nodiscard]] bool macWarnBeforeQuit() const {
		return _macWarnBeforeQuit;
	}
	void setChatQuickAction(HistoryView::DoubleClickQuickAction value) {
		_chatQuickAction = value;
	}
	[[nodiscard]] HistoryView::DoubleClickQuickAction chatQuickAction() const {
		return _chatQuickAction;
	}

	void setTranslateButtonEnabled(bool value);
	[[nodiscard]] bool translateButtonEnabled() const;
	void setTranslateChatEnabled(bool value);
	[[nodiscard]] bool translateChatEnabled() const;
	[[nodiscard]] rpl::producer<bool> translateChatEnabledValue() const;
	void setTranslateTo(LanguageId id);
	[[nodiscard]] LanguageId translateTo() const;
	[[nodiscard]] rpl::producer<LanguageId> translateToValue() const;
	void setSkipTranslationLanguages(std::vector<LanguageId> languages);
	[[nodiscard]] std::vector<LanguageId> skipTranslationLanguages() const;
	[[nodiscard]] auto skipTranslationLanguagesValue() const
		-> rpl::producer<std::vector<LanguageId>>;

	void setRememberedDeleteMessageOnlyForYou(bool value);
	[[nodiscard]] bool rememberedDeleteMessageOnlyForYou() const;

	[[nodiscard]] const WindowPosition &mediaViewPosition() const {
		return _mediaViewPosition;
	}
	void setMediaViewPosition(const WindowPosition &position) {
		_mediaViewPosition = position;
	}
	[[nodiscard]] bool ignoreBatterySaving() const {
		return _ignoreBatterySaving.current();
	}
	[[nodiscard]] rpl::producer<bool> ignoreBatterySavingValue() const {
		return _ignoreBatterySaving.value();
	}
	void setIgnoreBatterySavingValue(bool value) {
		_ignoreBatterySaving = value;
	}
	void setMacRoundIconDigest(std::optional<uint64> value) {
		_macRoundIconDigest = value;
	}
	[[nodiscard]] std::optional<uint64> macRoundIconDigest() const {
		return _macRoundIconDigest;
	}
	[[nodiscard]] bool storiesClickTooltipHidden() const {
		return _storiesClickTooltipHidden.current();
	}
	[[nodiscard]] rpl::producer<bool> storiesClickTooltipHiddenValue() const {
		return _storiesClickTooltipHidden.value();
	}
	void setStoriesClickTooltipHidden(bool value) {
		_storiesClickTooltipHidden = value;
	}
	[[nodiscard]] bool ttlVoiceClickTooltipHidden() const {
		return _ttlVoiceClickTooltipHidden.current();
	}
	[[nodiscard]] rpl::producer<bool> ttlVoiceClickTooltipHiddenValue() const {
		return _ttlVoiceClickTooltipHidden.value();
	}
	void setTtlVoiceClickTooltipHidden(bool value) {
		_ttlVoiceClickTooltipHidden = value;
	}

	[[nodiscard]] const WindowPosition &ivPosition() const {
		return _ivPosition;
	}
	void setIvPosition(const WindowPosition &position) {
		_ivPosition = position;
	}

	[[nodiscard]] QString customFontFamily() const {
		return _customFontFamily;
	}
	void setCustomFontFamily(const QString &value) {
		_customFontFamily = value;
	}

	[[nodiscard]] static bool ThirdColumnByDefault();
	[[nodiscard]] static float64 DefaultDialogsWidthRatio();

	struct PlaybackSpeed {
		float64 value = Media::kSpedUpDefault;
		bool enabled = false;
	};
	[[nodiscard]] static qint32 SerializePlaybackSpeed(PlaybackSpeed speed);
	[[nodiscard]] static PlaybackSpeed DeserializePlaybackSpeed(
		qint32 speed);

	void resetOnLastLogout();

private:
	void resolveRecentEmoji() const;

	static constexpr auto kDefaultThirdColumnWidth = 0;
	static constexpr auto kDefaultDialogsWidthRatio = 5. / 14;
	static constexpr auto kDefaultBigDialogsWidthRatio = 0.275;

	struct RecentEmojiPreload {
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
	rpl::variable<QString> _playbackDeviceId;
	rpl::variable<QString> _captureDeviceId;
	rpl::variable<QString> _cameraDeviceId;
	rpl::variable<QString> _callPlaybackDeviceId;
	rpl::variable<QString> _callCaptureDeviceId;
	int _callOutputVolume = 100;
	int _callInputVolume = 100;
	bool _callAudioDuckingEnabled = true;
	bool _disableCallsLegacy = false;
	bool _groupCallPushToTalk = false;
	bool _groupCallNoiseSuppression = false;
	QByteArray _groupCallPushToTalkShortcut;
	crl::time _groupCallPushToTalkDelay = 20;
	Window::Theme::AccentColors _themesAccentColors;
	bool _lastSeenWarningSeen = false;
	Ui::SendFilesWay _sendFilesWay = Ui::SendFilesWay();
	Ui::InputSubmitSettings _sendSubmitWay = Ui::InputSubmitSettings();
	base::flat_map<QString, QString> _soundOverrides;
	base::flat_set<QString> _noWarningExtensions;
	bool _ipRevealWarning = true;
	bool _loopAnimatedStickers = true;
	rpl::variable<bool> _largeEmoji = true;
	rpl::variable<bool> _replaceEmoji = true;
	bool _suggestEmoji = true;
	bool _suggestStickersByEmoji = true;
	bool _suggestAnimatedEmoji = true;
	rpl::variable<bool> _cornerReaction = true;
	rpl::variable<bool> _spellcheckerEnabled = true;
	PlaybackSpeed _videoPlaybackSpeed;
	PlaybackSpeed _voicePlaybackSpeed;
	QByteArray _videoPipGeometry;
	rpl::variable<std::vector<int>> _dictionariesEnabled;
	rpl::variable<bool> _autoDownloadDictionaries = true;
	rpl::variable<bool> _mainMenuAccountsShown = true;
	mutable std::vector<RecentEmojiPreload> _recentEmojiPreload;
	mutable std::vector<RecentEmoji> _recentEmoji;
	base::flat_set<QString> _recentEmojiSkip;
	mutable bool _recentEmojiResolved = false;
	base::flat_map<QString, uint8> _emojiVariants;
	rpl::event_stream<> _recentEmojiUpdated;
	bool _tabbedSelectorSectionEnabled = false; // per-window
	Window::Column _floatPlayerColumn = Window::Column(); // per-window
	RectPart _floatPlayerCorner = RectPart(); // per-window
	bool _thirdSectionInfoEnabled = true; // per-window
	rpl::event_stream<bool> _thirdSectionInfoEnabledValue; // per-window
	int _thirdSectionExtendedBy = -1; // per-window
	rpl::variable<float64> _dialogsWithChatWidthRatio; // per-window
	rpl::variable<float64> _dialogsNoChatWidthRatio; // per-window
	rpl::variable<int> _thirdColumnWidth = kDefaultThirdColumnWidth; // p-w
	bool _notifyFromAll = true;
	rpl::variable<bool> _nativeWindowFrame = false;
	rpl::variable<std::optional<bool>> _systemDarkMode = std::nullopt;
	rpl::variable<bool> _systemDarkModeEnabled = false;
	rpl::variable<WindowTitleContent> _windowTitleContent;
	WindowPosition _windowPosition; // per-window
	bool _disableOpenGL = false;
	rpl::variable<WorkMode> _workMode = WorkMode::WindowAndTray;
	base::flags<Calls::Group::StickedTooltip> _hiddenGroupCallTooltips;
	rpl::variable<bool> _closeToTaskbar = false;
	rpl::variable<bool> _trayIconMonochrome = true;
	rpl::variable<QString> _customDeviceModel;
	rpl::variable<Media::RepeatMode> _playerRepeatMode;
	rpl::variable<Media::OrderMode> _playerOrderMode;
	bool _macWarnBeforeQuit = true;
	std::vector<uint64> _accountsOrder;
#ifdef Q_OS_MAC
	bool _hardwareAcceleratedVideo = true;
#else // Q_OS_MAC
	bool _hardwareAcceleratedVideo = false;
#endif // Q_OS_MAC
	HistoryView::DoubleClickQuickAction _chatQuickAction
		= HistoryView::DoubleClickQuickAction();
	bool _translateButtonEnabled = false;
	rpl::variable<bool> _translateChatEnabled = true;
	rpl::variable<int> _translateToRaw = 0;
	rpl::variable<std::vector<LanguageId>> _skipTranslationLanguages;
	rpl::event_stream<> _skipTranslationLanguagesChanges;
	bool _rememberedDeleteMessageOnlyForYou = false;
	WindowPosition _mediaViewPosition = { .maximized = 2 };
	rpl::variable<bool> _ignoreBatterySaving = false;
	std::optional<uint64> _macRoundIconDigest;
	rpl::variable<bool> _storiesClickTooltipHidden = false;
	rpl::variable<bool> _ttlVoiceClickTooltipHidden = false;
	WindowPosition _ivPosition;
	QString _customFontFamily;

	bool _tabbedReplacedWithInfo = false; // per-window
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue; // per-window

	rpl::event_stream<> _saveDelayed;
	float64 _rememberedSongVolume = kDefaultVolume;
	bool _rememberedSoundNotifyFromTray = false;
	bool _rememberedFlashBounceNotifyFromTray = false;
	bool _dialogsWidthSetToZeroWithoutChat = false;

	QByteArray _photoEditorBrush;

};

} // namespace Core

