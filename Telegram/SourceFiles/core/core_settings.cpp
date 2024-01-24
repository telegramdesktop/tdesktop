/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings.h"

#include "base/platform/base_platform_info.h"
#include "calls/group/calls_group_common.h"
#include "history/view/history_view_quick_action.h"
#include "lang/lang_keys.h"
#include "platform/platform_notifications_manager.h"
#include "spellcheck/spellcheck_types.h"
#include "storage/serialize_common.h"
#include "ui/gl/gl_detection.h"
#include "ui/widgets/fields/input_field.h"
#include "webrtc/webrtc_create_adm.h"
#include "webrtc/webrtc_device_common.h"
#include "window/section_widget.h"

namespace Core {
namespace {

[[nodiscard]] WindowPosition Deserialize(const QByteArray &data) {
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);

	auto result = WindowPosition();
	stream
		>> result.x
		>> result.y
		>> result.w
		>> result.h
		>> result.moncrc
		>> result.maximized
		>> result.scale;
	return result;
}

void LogPosition(const WindowPosition &position, const QString &name) {
	DEBUG_LOG(("%1 Pos: Writing to storage %2, %3, %4, %5"
		" (scale %6%, maximized %7)")
		.arg(name)
		.arg(position.x)
		.arg(position.y)
		.arg(position.w)
		.arg(position.h)
		.arg(position.scale)
		.arg(position.maximized));
}

[[nodiscard]] QByteArray Serialize(const WindowPosition &position) {
	auto result = QByteArray();
	const auto size = 7 * sizeof(qint32);
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< qint32(position.x)
			<< qint32(position.y)
			<< qint32(position.w)
			<< qint32(position.h)
			<< qint32(position.moncrc)
			<< qint32(position.maximized)
			<< qint32(position.scale);
	}
	return result;
}

[[nodiscard]] QString Serialize(RecentEmojiDocument document) {
	return u"%1-%2"_q.arg(document.id).arg(document.test ? 1 : 0);
}

[[nodiscard]] std::optional<RecentEmojiDocument> ParseRecentEmojiDocument(
		const QString &serialized) {
	const auto parts = QStringView(serialized).split('-');
	if (parts.size() != 2 || parts[1].size() != 1) {
		return {};
	}
	const auto id = parts[0].toULongLong();
	const auto test = parts[1][0];
	if (!id || (test != '0' && test != '1')) {
		return {};
	}
	return RecentEmojiDocument{ id, (test == '1') };
}

} // namespace

[[nodiscard]] WindowPosition AdjustToScale(
		WindowPosition position,
		const QString &name) {
	DEBUG_LOG(("%1 Pos: Initializing first %2, %3, %4, %5 "
		"(scale %6%, maximized %7)")
		.arg(name)
		.arg(position.x)
		.arg(position.y)
		.arg(position.w)
		.arg(position.h)
		.arg(position.scale)
		.arg(position.maximized));

	if (!position.scale) {
		return position;
	}
	const auto scaleFactor = cScale() / float64(position.scale);
	if (scaleFactor != 1.) {
		// Change scale while keeping the position center in place.
		position.x += position.w / 2;
		position.y += position.h / 2;
		position.w *= scaleFactor;
		position.h *= scaleFactor;
		position.x -= position.w / 2;
		position.y -= position.h / 2;
	}
	return position;
}

Settings::Settings()
: _sendSubmitWay(Ui::InputSubmitSettings::Enter)
, _floatPlayerColumn(Window::Column::Second)
, _floatPlayerCorner(RectPart::TopRight)
, _dialogsWidthRatio(DefaultDialogsWidthRatio()) {
}

Settings::~Settings() = default;

QByteArray Settings::serialize() const {
	const auto themesAccentColors = _themesAccentColors.serialize();
	const auto windowPosition = Serialize(_windowPosition);
	LogPosition(_windowPosition, u"Window"_q);
	const auto mediaViewPosition = Serialize(_mediaViewPosition);
	LogPosition(_mediaViewPosition, u"Viewer"_q);
	const auto proxy = _proxy.serialize();
	const auto skipLanguages = _skipTranslationLanguages.current();

	auto recentEmojiPreloadGenerated = std::vector<RecentEmojiPreload>();
	if (_recentEmojiPreload.empty()) {
		recentEmojiPreloadGenerated.reserve(_recentEmoji.size());
		for (const auto &[id, rating] : _recentEmoji) {
			auto string = QString();
			if (const auto document = std::get_if<RecentEmojiDocument>(
					&id.data)) {
				string = Serialize(*document);
			} else if (const auto emoji = std::get_if<EmojiPtr>(&id.data)) {
				string = (*emoji)->id();
			}
			recentEmojiPreloadGenerated.push_back({ string, rating });
		}
	}
	const auto &recentEmojiPreloadData = _recentEmojiPreload.empty()
		? recentEmojiPreloadGenerated
		: _recentEmojiPreload;

	auto size = Serialize::bytearraySize(themesAccentColors)
		+ sizeof(qint32) * 5
		+ Serialize::stringSize(_downloadPath.current())
		+ Serialize::bytearraySize(_downloadPathBookmark)
		+ sizeof(qint32) * 9
		+ Serialize::stringSize(QString()) // legacy call output device id
		+ Serialize::stringSize(QString()) // legacy call input device id
		+ sizeof(qint32) * 5;
	for (const auto &[key, value] : _soundOverrides) {
		size += Serialize::stringSize(key) + Serialize::stringSize(value);
	}
	size += sizeof(qint32) * 13
		+ Serialize::bytearraySize(_videoPipGeometry)
		+ sizeof(qint32)
		+ (_dictionariesEnabled.current().size() * sizeof(quint64))
		+ sizeof(qint32) * 12
		+ Serialize::stringSize(_cameraDeviceId.current())
		+ sizeof(qint32) * 2
		+ Serialize::bytearraySize(_groupCallPushToTalkShortcut)
		+ sizeof(qint64)
		+ sizeof(qint32) * 2
		+ Serialize::bytearraySize(windowPosition)
		+ sizeof(qint32);
	for (const auto &[id, rating] : recentEmojiPreloadData) {
		size += Serialize::stringSize(id) + sizeof(quint16);
	}
	size += sizeof(qint32);
	for (const auto &[id, variant] : _emojiVariants) {
		size += Serialize::stringSize(id) + sizeof(quint8);
	}
	size += sizeof(qint32) * 3
		+ Serialize::bytearraySize(proxy)
		+ sizeof(qint32) * 2
		+ Serialize::bytearraySize(_photoEditorBrush)
		+ sizeof(qint32) * 3
		+ Serialize::stringSize(_customDeviceModel.current())
		+ sizeof(qint32) * 4
		+ (_accountsOrder.size() * sizeof(quint64))
		+ sizeof(qint32) * 7
		+ (skipLanguages.size() * sizeof(quint64))
		+ sizeof(qint32) * 2
		+ sizeof(quint64)
		+ sizeof(qint32) * 3
		+ Serialize::bytearraySize(mediaViewPosition)
		+ sizeof(qint32)
		+ sizeof(quint64)
		+ sizeof(qint32) * 2;
	for (const auto &id : _recentEmojiSkip) {
		size += Serialize::stringSize(id);
	}
	size += sizeof(qint32) * 2
		+ Serialize::stringSize(_playbackDeviceId.current())
		+ Serialize::stringSize(_captureDeviceId.current())
		+ Serialize::stringSize(_callPlaybackDeviceId.current())
		+ Serialize::stringSize(_callCaptureDeviceId.current());

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< themesAccentColors
			<< qint32(_adaptiveForWide.current() ? 1 : 0)
			<< qint32(_moderateModeEnabled ? 1 : 0)
			<< qint32(qRound(_songVolume.current() * 1e6))
			<< qint32(qRound(_videoVolume.current() * 1e6))
			<< qint32(_askDownloadPath ? 1 : 0)
			<< _downloadPath.current()
			<< _downloadPathBookmark
			<< qint32(1)
			<< qint32(_soundNotify ? 1 : 0)
			<< qint32(_desktopNotify ? 1 : 0)
			<< qint32(_flashBounceNotify ? 1 : 0)
			<< static_cast<qint32>(_notifyView)
			<< qint32(_nativeNotifications ? (*_nativeNotifications ? 1 : 2) : 0)
			<< qint32(_notificationsCount)
			<< static_cast<qint32>(_notificationsCorner)
			<< qint32(_autoLock)
			<< QString() // legacy call output device id
			<< QString() // legacy call input device id
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
			<< qint32(SerializePlaybackSpeed(_videoPlaybackSpeed))
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
			<< qint32(std::clamp(
				qRound(_dialogsWidthRatio.current() * 1000000),
				0,
				1000000))
			<< qint32(_thirdColumnWidth.current())
			<< qint32(_thirdSectionExtendedBy)
			<< qint32(_notifyFromAll ? 1 : 0)
			<< qint32(_nativeWindowFrame.current() ? 1 : 0)
			<< qint32(_systemDarkModeEnabled.current() ? 1 : 0)
			<< _cameraDeviceId.current()
			<< qint32(_ipRevealWarning ? 1 : 0)
			<< qint32(_groupCallPushToTalk ? 1 : 0)
			<< _groupCallPushToTalkShortcut
			<< qint64(_groupCallPushToTalkDelay)
			<< qint32(0) // Call audio backend
			<< qint32(0) // Legacy disable calls, now in session settings
			<< windowPosition
			<< qint32(recentEmojiPreloadData.size());
		for (const auto &[id, rating] : recentEmojiPreloadData) {
			stream << id << quint16(rating);
		}
		stream
			<< qint32(_emojiVariants.size());
		for (const auto &[id, variant] : _emojiVariants) {
			stream << id << quint8(variant);
		}
		stream
			<< qint32(0) // Old Disable OpenGL
			<< qint32(0) // Old Noise Suppression
			<< qint32(_workMode.current())
			<< proxy
			<< qint32(_hiddenGroupCallTooltips.value())
			<< qint32(_disableOpenGL ? 1 : 0)
			<< _photoEditorBrush
			<< qint32(_groupCallNoiseSuppression ? 1 : 0)
			<< qint32(SerializePlaybackSpeed(_voicePlaybackSpeed))
			<< qint32(_closeToTaskbar.current() ? 1 : 0)
			<< _customDeviceModel.current()
			<< qint32(_playerRepeatMode.current())
			<< qint32(_playerOrderMode.current())
			<< qint32(_macWarnBeforeQuit ? 1 : 0);

		stream
			<< qint32(_accountsOrder.size());
		for (const auto &id : _accountsOrder) {
			stream << quint64(id);
		}

		stream
			<< qint32(0) // old hardwareAcceleratedVideo
			<< qint32(_chatQuickAction)
			<< qint32(_hardwareAcceleratedVideo ? 1 : 0)
			<< qint32(_suggestAnimatedEmoji ? 1 : 0)
			<< qint32(_cornerReaction.current() ? 1 : 0)
			<< qint32(_translateButtonEnabled ? 1 : 0);

		stream
			<< qint32(skipLanguages.size());
		for (const auto &id : skipLanguages) {
			stream << quint64(id.value);
		}

		stream
			<< qint32(_rememberedDeleteMessageOnlyForYou ? 1 : 0)
			<< qint32(_translateChatEnabled.current() ? 1 : 0)
			<< quint64(QLocale::Language(_translateToRaw.current()))
			<< qint32(_windowTitleContent.current().hideChatName ? 1 : 0)
			<< qint32(_windowTitleContent.current().hideAccountName ? 1 : 0)
			<< qint32(_windowTitleContent.current().hideTotalUnread ? 1 : 0)
			<< mediaViewPosition
			<< qint32(_ignoreBatterySaving.current() ? 1 : 0)
			<< quint64(_macRoundIconDigest.value_or(0))
			<< qint32(_storiesClickTooltipHidden.current() ? 1 : 0)
			<< qint32(_recentEmojiSkip.size());
		for (const auto &id : _recentEmojiSkip) {
			stream << id;
		}
		stream
			<< qint32(_trayIconMonochrome.current() ? 1 : 0)
			<< qint32(_ttlVoiceClickTooltipHidden.current() ? 1 : 0)
			<< _playbackDeviceId.current()
			<< _captureDeviceId.current()
			<< _callPlaybackDeviceId.current()
			<< _callCaptureDeviceId.current();
	}

	Ensures(result.size() == size);
	return result;
}

void Settings::addFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	QByteArray themesAccentColors;
	qint32 adaptiveForWide = _adaptiveForWide.current() ? 1 : 0;
	qint32 moderateModeEnabled = _moderateModeEnabled ? 1 : 0;
	qint32 songVolume = qint32(qRound(_songVolume.current() * 1e6));
	qint32 videoVolume = qint32(qRound(_videoVolume.current() * 1e6));
	qint32 askDownloadPath = _askDownloadPath ? 1 : 0;
	QString downloadPath = _downloadPath.current();
	QByteArray downloadPathBookmark = _downloadPathBookmark;
	qint32 nonDefaultVoicePlaybackSpeed = 1;
	qint32 soundNotify = _soundNotify ? 1 : 0;
	qint32 desktopNotify = _desktopNotify ? 1 : 0;
	qint32 flashBounceNotify = _flashBounceNotify ? 1 : 0;
	qint32 notifyView = static_cast<qint32>(_notifyView);
	qint32 nativeNotifications = _nativeNotifications ? (*_nativeNotifications ? 1 : 2) : 0;
	qint32 notificationsCount = _notificationsCount;
	qint32 notificationsCorner = static_cast<qint32>(_notificationsCorner);
	qint32 autoLock = _autoLock;
	QString playbackDeviceId = _playbackDeviceId.current();
	QString captureDeviceId = _captureDeviceId.current();
	QString cameraDeviceId = _cameraDeviceId.current();
	QString legacyCallPlaybackDeviceId = _callPlaybackDeviceId.current();
	QString legacyCallCaptureDeviceId = _callCaptureDeviceId.current();
	QString callPlaybackDeviceId = _callPlaybackDeviceId.current();
	QString callCaptureDeviceId = _callCaptureDeviceId.current();
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
	qint32 videoPlaybackSpeed = SerializePlaybackSpeed(_videoPlaybackSpeed);
	qint32 voicePlaybackSpeed = SerializePlaybackSpeed(_voicePlaybackSpeed);
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
	qint32 legacyCallAudioBackend = 0;
	qint32 disableCallsLegacy = 0;
	QByteArray windowPosition;
	std::vector<RecentEmojiPreload> recentEmojiPreload;
	base::flat_map<QString, uint8> emojiVariants;
	qint32 disableOpenGL = _disableOpenGL ? 1 : 0;
	qint32 groupCallNoiseSuppression = _groupCallNoiseSuppression ? 1 : 0;
	qint32 workMode = static_cast<qint32>(_workMode.current());
	QByteArray proxy;
	qint32 hiddenGroupCallTooltips = qint32(_hiddenGroupCallTooltips.value());
	QByteArray photoEditorBrush = _photoEditorBrush;
	qint32 closeToTaskbar = _closeToTaskbar.current() ? 1 : 0;
	QString customDeviceModel = _customDeviceModel.current();
	qint32 playerRepeatMode = static_cast<qint32>(_playerRepeatMode.current());
	qint32 playerOrderMode = static_cast<qint32>(_playerOrderMode.current());
	qint32 macWarnBeforeQuit = _macWarnBeforeQuit ? 1 : 0;
	qint32 accountsOrderCount = 0;
	std::vector<uint64> accountsOrder;
	qint32 hardwareAcceleratedVideo = _hardwareAcceleratedVideo ? 1 : 0;
	qint32 chatQuickAction = static_cast<qint32>(_chatQuickAction);
	qint32 suggestAnimatedEmoji = _suggestAnimatedEmoji ? 1 : 0;
	qint32 cornerReaction = _cornerReaction.current() ? 1 : 0;
	qint32 legacySkipTranslationForLanguage = _translateButtonEnabled ? 1 : 0;
	qint32 skipTranslationLanguagesCount = 0;
	std::vector<LanguageId> skipTranslationLanguages;
	qint32 rememberedDeleteMessageOnlyForYou = _rememberedDeleteMessageOnlyForYou ? 1 : 0;
	qint32 translateChatEnabled = _translateChatEnabled.current() ? 1 : 0;
	quint64 translateToRaw = _translateToRaw.current();
	qint32 hideChatName = _windowTitleContent.current().hideChatName ? 1 : 0;
	qint32 hideAccountName = _windowTitleContent.current().hideAccountName ? 1 : 0;
	qint32 hideTotalUnread = _windowTitleContent.current().hideTotalUnread ? 1 : 0;
	QByteArray mediaViewPosition;
	qint32 ignoreBatterySaving = _ignoreBatterySaving.current() ? 1 : 0;
	quint64 macRoundIconDigest = _macRoundIconDigest.value_or(0);
	qint32 storiesClickTooltipHidden = _storiesClickTooltipHidden.current() ? 1 : 0;
	base::flat_set<QString> recentEmojiSkip;
	qint32 trayIconMonochrome = (_trayIconMonochrome.current() ? 1 : 0);
	qint32 ttlVoiceClickTooltipHidden = _ttlVoiceClickTooltipHidden.current() ? 1 : 0;

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
			>> nonDefaultVoicePlaybackSpeed
			>> soundNotify
			>> desktopNotify
			>> flashBounceNotify
			>> notifyView
			>> nativeNotifications
			>> notificationsCount
			>> notificationsCorner
			>> autoLock
			>> legacyCallPlaybackDeviceId
			>> legacyCallCaptureDeviceId
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
		dialogsWidthRatio = std::clamp(
			dialogsWidthRatioInt / 1000000.,
			0.,
			1.);
	}
	if (!stream.atEnd()) {
		stream >> nativeWindowFrame;
	}
	if (!stream.atEnd()) {
		stream >> systemDarkModeEnabled;
	}
	if (!stream.atEnd()) {
		stream >> cameraDeviceId;
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
		stream >> legacyCallAudioBackend;
	}
	if (!stream.atEnd()) {
		stream >> disableCallsLegacy;
	}
	if (!stream.atEnd()) {
		stream >> windowPosition;
	}
	if (!stream.atEnd()) {
		auto recentCount = qint32(0);
		stream >> recentCount;
		if (recentCount > 0 && recentCount < 10000) {
			recentEmojiPreload.reserve(recentCount);
			for (auto i = 0; i != recentCount; ++i) {
				auto id = QString();
				auto rating = quint16();
				stream >> id >> rating;
				recentEmojiPreload.push_back({ id, rating });
			}
		}
		auto variantsCount = qint32(0);
		stream >> variantsCount;
		if (variantsCount > 0 && variantsCount < 10000) {
			emojiVariants.reserve(variantsCount);
			for (auto i = 0; i != variantsCount; ++i) {
				auto id = QString();
				auto variant = quint8();
				stream >> id >> variant;
				emojiVariants.emplace(id, variant);
			}
		}
	}
	if (!stream.atEnd()) {
		qint32 disableOpenGLOld;
		stream >> disableOpenGLOld;
	}
	if (!stream.atEnd()) {
		qint32 groupCallNoiseSuppressionOld;
		stream >> groupCallNoiseSuppressionOld;
	}
	if (!stream.atEnd()) {
		stream >> workMode;
	}
	if (!stream.atEnd()) {
		stream >> proxy;
	}
	if (!stream.atEnd()) {
		stream >> hiddenGroupCallTooltips;
	}
	if (!stream.atEnd()) {
		stream >> disableOpenGL;
	}
	if (!stream.atEnd()) {
		stream >> photoEditorBrush;
	}
	if (!stream.atEnd()) {
		stream >> groupCallNoiseSuppression;
	}
	if (!stream.atEnd()) {
		stream >> voicePlaybackSpeed;
	}
	if (!stream.atEnd()) {
		stream >> closeToTaskbar;
	}
	if (!stream.atEnd()) {
		stream >> customDeviceModel;
	}
	if (!stream.atEnd()) {
		stream
			>> playerRepeatMode
			>> playerOrderMode;
	}
	if (!stream.atEnd()) {
		stream >> macWarnBeforeQuit;
	}
	if (!stream.atEnd()) {
		stream >> accountsOrderCount;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != accountsOrderCount; ++i) {
				quint64 sessionUniqueId;
				stream >> sessionUniqueId;
				accountsOrder.emplace_back(sessionUniqueId);
			}
		}
	}
	if (!stream.atEnd()) {
		qint32 legacyHardwareAcceleratedVideo = 0;
		stream >> legacyHardwareAcceleratedVideo;
	}
	if (!stream.atEnd()) {
		stream >> chatQuickAction;
	}
	if (!stream.atEnd()) {
		stream >> hardwareAcceleratedVideo;
	}
	if (!stream.atEnd()) {
		stream >> suggestAnimatedEmoji;
	}
	if (!stream.atEnd()) {
		stream >> cornerReaction;
	}
	if (!stream.atEnd()) {
		stream >> legacySkipTranslationForLanguage;
	}
	if (!stream.atEnd()) {
		stream >> skipTranslationLanguagesCount;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != skipTranslationLanguagesCount; ++i) {
				quint64 language;
				stream >> language;
				skipTranslationLanguages.push_back({
					QLocale::Language(language)
				});
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> rememberedDeleteMessageOnlyForYou;
	}
	if (!stream.atEnd()) {
		stream
			>> translateChatEnabled
			>> translateToRaw;
	}
	if (!stream.atEnd()) {
		stream
			>> hideChatName
			>> hideAccountName
			>> hideTotalUnread;
	}
	if (!stream.atEnd()) {
		stream >> mediaViewPosition;
	}
	if (!stream.atEnd()) {
		stream >> ignoreBatterySaving;
	}
	if (!stream.atEnd()) {
		stream >> macRoundIconDigest;
	}
	if (!stream.atEnd()) {
		stream >> storiesClickTooltipHidden;
	}
	if (!stream.atEnd()) {
		auto count = qint32();
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				auto id = QString();
				stream >> id;
				if (stream.status() == QDataStream::Ok) {
					recentEmojiSkip.emplace(id);
				}
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> trayIconMonochrome;
	} else {
		// Let existing clients use the old value.
		trayIconMonochrome = 0;
	}
	if (!stream.atEnd()) {
		stream >> ttlVoiceClickTooltipHidden;
	}
	if (!stream.atEnd()) {
		stream
			>> playbackDeviceId
			>> captureDeviceId;
	}
	if (!stream.atEnd()) {
		stream
			>> callPlaybackDeviceId
			>> callCaptureDeviceId;
	} else {
		const auto &defaultId = Webrtc::kDefaultDeviceId;
		callPlaybackDeviceId = (legacyCallPlaybackDeviceId == defaultId)
			? QString()
			: legacyCallPlaybackDeviceId;
		callCaptureDeviceId = (legacyCallCaptureDeviceId == defaultId)
			? QString()
			: legacyCallCaptureDeviceId;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for Core::Settings::constructFromSerialized()"));
		return;
	} else if (!_themesAccentColors.setFromSerialized(themesAccentColors)) {
		return;
	} else if (!_proxy.setFromSerialized(proxy)) {
		return;
	}
	_adaptiveForWide = (adaptiveForWide == 1);
	_moderateModeEnabled = (moderateModeEnabled == 1);
	_songVolume = std::clamp(songVolume / 1e6, 0., 1.);
	_videoVolume = std::clamp(videoVolume / 1e6, 0., 1.);
	_askDownloadPath = (askDownloadPath == 1);
	_downloadPath = downloadPath;
	_downloadPathBookmark = downloadPathBookmark;
	_soundNotify = (soundNotify == 1);
	_desktopNotify = (desktopNotify == 1);
	_flashBounceNotify = (flashBounceNotify == 1);
	const auto uncheckedNotifyView = static_cast<NotifyView>(notifyView);
	switch (uncheckedNotifyView) {
	case NotifyView::ShowNothing:
	case NotifyView::ShowName:
	case NotifyView::ShowPreview: _notifyView = uncheckedNotifyView; break;
	}
	switch (nativeNotifications) {
	case 0: _nativeNotifications = std::nullopt; break;
	case 1: _nativeNotifications = true; break;
	case 2: _nativeNotifications = false; break;
	default: break;
	}
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
	_playbackDeviceId = playbackDeviceId;
	_captureDeviceId = captureDeviceId;
	const auto kOldDefault = u"default"_q;
	_cameraDeviceId = cameraDeviceId;
	_callPlaybackDeviceId = callPlaybackDeviceId;
	_callCaptureDeviceId = callCaptureDeviceId;
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
	_voicePlaybackSpeed = DeserializePlaybackSpeed(voicePlaybackSpeed);
	if (nonDefaultVoicePlaybackSpeed != 1) {
		_voicePlaybackSpeed.enabled = false;
	}
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
	_disableCallsLegacy = (disableCallsLegacy == 1);
	if (!windowPosition.isEmpty()) {
		_windowPosition = Deserialize(windowPosition);
	}
	_recentEmojiPreload = std::move(recentEmojiPreload);
	_emojiVariants = std::move(emojiVariants);
	_disableOpenGL = (disableOpenGL == 1);
	if (!Platform::IsMac()) {
		Ui::GL::ForceDisable(_disableOpenGL
			|| Ui::GL::LastCrashCheckFailed());
	}
	_groupCallNoiseSuppression = (groupCallNoiseSuppression == 1);
	const auto uncheckedWorkMode = static_cast<WorkMode>(workMode);
	switch (uncheckedWorkMode) {
	case WorkMode::WindowAndTray:
	case WorkMode::TrayOnly:
	case WorkMode::WindowOnly: _workMode = uncheckedWorkMode; break;
	}
	_hiddenGroupCallTooltips = [&] {
		using Tooltip = Calls::Group::StickedTooltip;
		return Tooltip(0)
			| ((hiddenGroupCallTooltips & int(Tooltip::Camera))
				? Tooltip::Camera
				: Tooltip(0))
			| ((hiddenGroupCallTooltips & int(Tooltip::Microphone))
				? Tooltip::Microphone
				: Tooltip(0));
	}();
	_photoEditorBrush = photoEditorBrush;
	_closeToTaskbar = (closeToTaskbar == 1);
	_customDeviceModel = customDeviceModel;
	_accountsOrder = accountsOrder;
	const auto uncheckedPlayerRepeatMode = static_cast<Media::RepeatMode>(playerRepeatMode);
	switch (uncheckedPlayerRepeatMode) {
	case Media::RepeatMode::None:
	case Media::RepeatMode::One:
	case Media::RepeatMode::All: _playerRepeatMode = uncheckedPlayerRepeatMode; break;
	}
	const auto uncheckedPlayerOrderMode = static_cast<Media::OrderMode>(playerOrderMode);
	switch (uncheckedPlayerOrderMode) {
	case Media::OrderMode::Default:
	case Media::OrderMode::Reverse:
	case Media::OrderMode::Shuffle: _playerOrderMode = uncheckedPlayerOrderMode; break;
	}
	_macWarnBeforeQuit = (macWarnBeforeQuit == 1);
	_hardwareAcceleratedVideo = (hardwareAcceleratedVideo == 1);
	{
		using Quick = HistoryView::DoubleClickQuickAction;
		const auto uncheckedChatQuickAction = static_cast<Quick>(
			chatQuickAction);
		switch (uncheckedChatQuickAction) {
		case Quick::None:
		case Quick::Reply:
		case Quick::React: _chatQuickAction = uncheckedChatQuickAction; break;
		}
	}
	_suggestAnimatedEmoji = (suggestAnimatedEmoji == 1);
	_cornerReaction = (cornerReaction == 1);
	{ // Parse the legacy translation setting.
		if (legacySkipTranslationForLanguage == 0) {
			_translateButtonEnabled = false;
		} else if (legacySkipTranslationForLanguage == 1) {
			_translateButtonEnabled = true;
		} else {
			_translateButtonEnabled = (legacySkipTranslationForLanguage > 0);
			skipTranslationLanguages.push_back({
				QLocale::Language(std::abs(legacySkipTranslationForLanguage))
			});
		}
		_skipTranslationLanguages = std::move(skipTranslationLanguages);
	}
	_rememberedDeleteMessageOnlyForYou = (rememberedDeleteMessageOnlyForYou == 1);
	_translateChatEnabled = (translateChatEnabled == 1);
	_translateToRaw = int(QLocale::Language(translateToRaw));
	_windowTitleContent = WindowTitleContent{
		.hideChatName = (hideChatName == 1),
		.hideAccountName = (hideAccountName == 1),
		.hideTotalUnread = (hideTotalUnread == 1),
	};
	if (!mediaViewPosition.isEmpty()) {
		_mediaViewPosition = Deserialize(mediaViewPosition);
		if (!_mediaViewPosition.w && !_mediaViewPosition.maximized) {
			_mediaViewPosition = { .maximized = 2 };
		}
	}
	_ignoreBatterySaving = (ignoreBatterySaving == 1);
	_macRoundIconDigest = macRoundIconDigest ? macRoundIconDigest : std::optional<uint64>();
	_storiesClickTooltipHidden = (storiesClickTooltipHidden == 1);
	_recentEmojiSkip = std::move(recentEmojiSkip);
	_trayIconMonochrome = (trayIconMonochrome == 1);
	_ttlVoiceClickTooltipHidden = (ttlVoiceClickTooltipHidden == 1);
}

QString Settings::getSoundPath(const QString &key) const {
	auto it = _soundOverrides.find(key);
	if (it != _soundOverrides.end()) {
		return it->second;
	}
	return u":/sounds/"_q + key + u".mp3"_q;
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

QString Settings::deviceModel() const {
	const auto custom = customDeviceModel();
	return custom.isEmpty() ? Platform::DeviceModelPretty() : custom;
}

rpl::producer<QString> Settings::deviceModelChanges() const {
	return customDeviceModelChanges() | rpl::map([=] {
		return deviceModel();
	});
}

rpl::producer<QString> Settings::deviceModelValue() const {
	return customDeviceModelValue() | rpl::map([=] {
		return deviceModel();
	});
}

int Settings::thirdColumnWidth() const {
	return _thirdColumnWidth.current();
}

rpl::producer<int> Settings::thirdColumnWidthChanges() const {
	return _thirdColumnWidth.changes();
}

const std::vector<RecentEmoji> &Settings::recentEmoji() const {
	if (!_recentEmojiResolved) {
		_recentEmojiResolved = true;
		resolveRecentEmoji();
	}
	return _recentEmoji;
}

void Settings::resolveRecentEmoji() const {
	const auto haveAlready = [&](RecentEmojiId id) {
		return ranges::contains(
			_recentEmoji,
			id,
			[](const RecentEmoji &data) { return data.id; });
	};
	auto testCount = 0;
	auto nonTestCount = 0;
	if (!_recentEmojiPreload.empty()) {
		_recentEmoji.reserve(_recentEmojiPreload.size());
		for (const auto &[id, rating] : base::take(_recentEmojiPreload)) {
			auto length = int();
			const auto emoji = Ui::Emoji::Find(id, &length);
			if (emoji && length == id.size()) {
				if (!haveAlready({ emoji })) {
					_recentEmoji.push_back({ { emoji }, rating });
				}
			} else if (const auto document = ParseRecentEmojiDocument(id)) {
				if (!haveAlready({ *document })) {
					_recentEmoji.push_back({ { *document }, rating });
					if (document->test) {
						++testCount;
					} else {
						++nonTestCount;
					}
				}
			}
		}
		_recentEmojiPreload.clear();
	}
	const auto specialCount = std::max(testCount, nonTestCount);
	for (const auto emoji : Ui::Emoji::GetDefaultRecent()) {
		if (_recentEmoji.size() >= specialCount + kRecentEmojiLimit) {
			break;
		} else if (_recentEmojiSkip.contains(emoji->id())) {
			continue;
		} else if (!haveAlready({ emoji })) {
			_recentEmoji.push_back({ { emoji }, 1 });
		}
	}
}

void Settings::incrementRecentEmoji(RecentEmojiId id) {
	resolveRecentEmoji();

	if (const auto emoji = std::get_if<EmojiPtr>(&id.data)) {
		_recentEmojiSkip.remove((*emoji)->id());
	}
	auto i = _recentEmoji.begin(), e = _recentEmoji.end();
	for (; i != e; ++i) {
		if (i->id == id) {
			++i->rating;
			if (i->rating > 0x8000) {
				for (auto j = _recentEmoji.begin(); j != e; ++j) {
					if (j->rating > 1) {
						j->rating /= 2;
					} else {
						j->rating = 1;
					}
				}
			}
			for (; i != _recentEmoji.begin(); --i) {
				if ((i - 1)->rating > i->rating) {
					break;
				}
				std::swap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		_recentEmoji.push_back({ id, 1 });
		for (i = _recentEmoji.end() - 1; i != _recentEmoji.begin(); --i) {
			if ((i - 1)->rating > i->rating) {
				break;
			}
			std::swap(*i, *(i - 1));
		}
		auto testCount = 0;
		auto nonTestCount = 0;
		for (const auto &emoji : _recentEmoji) {
			const auto id = &emoji.id.data;
			if (const auto document = std::get_if<RecentEmojiDocument>(id)) {
				if (document->test) {
					++testCount;
				} else {
					++nonTestCount;
				}
			}
		}
		const auto specialCount = std::max(testCount, nonTestCount);
		while (_recentEmoji.size() >= specialCount + kRecentEmojiLimit) {
			_recentEmoji.pop_back();
		}
	}
	_recentEmojiUpdated.fire({});
	_saveDelayed.fire({});
}

void Settings::hideRecentEmoji(RecentEmojiId id) {
	resolveRecentEmoji();

	_recentEmoji.erase(
		ranges::remove(_recentEmoji, id, &RecentEmoji::id),
		end(_recentEmoji));
	if (const auto emoji = std::get_if<EmojiPtr>(&id.data)) {
		for (const auto always : Ui::Emoji::GetDefaultRecent()) {
			if (always == *emoji) {
				_recentEmojiSkip.emplace(always->id());
				break;
			}
		}
	}
	_recentEmojiUpdated.fire({});
	_saveDelayed.fire({});
}

void Settings::resetRecentEmoji() {
	resolveRecentEmoji();

	_recentEmoji.clear();
	_recentEmojiSkip.clear();
	_recentEmojiPreload.clear();
	_recentEmojiResolved = false;

	_recentEmojiUpdated.fire({});
	_saveDelayed.fire({});
}

void Settings::setLegacyRecentEmojiPreload(
		QVector<QPair<QString, ushort>> data) {
	if (!_recentEmojiPreload.empty() || data.isEmpty()) {
		return;
	}
	_recentEmojiPreload.reserve(data.size());
	for (const auto &[id, rating] : data) {
		_recentEmojiPreload.push_back({ id, rating });
	}
}

EmojiPtr Settings::lookupEmojiVariant(EmojiPtr emoji) const {
	if (emoji->hasVariants()) {
		const auto i = _emojiVariants.find(emoji->nonColoredId());
		if (i != end(_emojiVariants)) {
			return emoji->variant(i->second);
		}
		const auto j = _emojiVariants.find(QString());
		if (j != end(_emojiVariants)) {
			return emoji->variant(j->second);
		}
	}
	return emoji;
}

bool Settings::hasChosenEmojiVariant(EmojiPtr emoji) const {
	return _emojiVariants.contains(QString())
		|| _emojiVariants.contains(emoji->nonColoredId());
}

void Settings::saveEmojiVariant(EmojiPtr emoji) {
	Expects(emoji->hasVariants());

	_emojiVariants[emoji->nonColoredId()] = emoji->variantIndex(emoji);
	_saveDelayed.fire({});
}

void Settings::saveAllEmojiVariants(EmojiPtr emoji) {
	Expects(emoji->hasVariants());

	_emojiVariants.clear();
	_emojiVariants[QString()] = emoji->variantIndex(emoji);
	_saveDelayed.fire({});
}

void Settings::setLegacyEmojiVariants(QMap<QString, int> data) {
	if (!_emojiVariants.empty() || data.isEmpty()) {
		return;
	}
	_emojiVariants.reserve(data.size());
	for (auto i = data.begin(), e = data.end(); i != e; ++i) {
		_emojiVariants.emplace(i.key(), i.value());
	}
}

void Settings::resetOnLastLogout() {
	_adaptiveForWide = true;
	_moderateModeEnabled = false;

	_songVolume = kDefaultVolume;
	_videoVolume = kDefaultVolume;

	_askDownloadPath = false;
	_downloadPath = QString();
	_downloadPathBookmark = QByteArray();

	_soundNotify = true;
	_desktopNotify = true;
	_flashBounceNotify = true;
	_notifyView = NotifyView::ShowPreview;
	//_nativeNotifications = std::nullopt;
	//_notificationsCount = 3;
	//_notificationsCorner = ScreenCorner::BottomRight;
	_includeMutedCounter = true;
	_countUnreadMessages = true;
	_notifyAboutPinned = true;
	//_autoLock = 3600;

	//_playbackDeviceId = QString();
	//_captureDeviceId = QString();
	//_cameraDeviceId = QString();
	//_callPlaybackDeviceId = QString();
	//_callCaptureDeviceId = QString();
	//_callOutputVolume = 100;
	//_callInputVolume = 100;
	//_callAudioDuckingEnabled = true;

	_disableCallsLegacy = false;

	_groupCallPushToTalk = false;
	_groupCallPushToTalkShortcut = QByteArray();
	_groupCallPushToTalkDelay = 20;

	_groupCallNoiseSuppression = false;

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
	_suggestAnimatedEmoji = true;
	_spellcheckerEnabled = true;
	_videoPlaybackSpeed = PlaybackSpeed();
	_voicePlaybackSpeed = PlaybackSpeed();
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
	_hiddenGroupCallTooltips = 0;
	_storiesClickTooltipHidden = false;
	_ttlVoiceClickTooltipHidden = false;

	_recentEmojiPreload.clear();
	_recentEmoji.clear();
	_emojiVariants.clear();

	_accountsOrder.clear();
}

bool Settings::ThirdColumnByDefault() {
	return Platform::IsMacStoreBuild();
}

float64 Settings::DefaultDialogsWidthRatio() {
	return ThirdColumnByDefault()
		? kDefaultBigDialogsWidthRatio
		: kDefaultDialogsWidthRatio;
}

qint32 Settings::SerializePlaybackSpeed(PlaybackSpeed speed) {
	using namespace Media;

	const auto value = int(base::SafeRound(
		std::clamp(speed.value, kSpeedMin, kSpeedMax) * 100));
	return speed.enabled ? value : -value;
}

auto Settings::DeserializePlaybackSpeed(qint32 speed) -> PlaybackSpeed {
	using namespace Media;

	auto enabled = true;
	const auto validate = [&](float64 result) {
		return PlaybackSpeed{
			.value = (result == 1.) ? kSpedUpDefault : result,
			.enabled = enabled && (result != 1.),
		};
	};
	if (speed >= 0 && speed < 10) {
		// The old values in settings.
		return validate((std::clamp(speed, 0, 6) + 2) / 4.);
	} else if (speed < 0) {
		speed = -speed;
		enabled = false;
	}
	return validate(std::clamp(speed / 100., kSpeedMin, kSpeedMax));
}

bool Settings::nativeNotifications() const {
	return _nativeNotifications.value_or(
		Platform::Notifications::ByDefault());
}

void Settings::setNativeNotifications(bool value) {
	_nativeNotifications = (value == Platform::Notifications::ByDefault())
		? std::nullopt
		: std::make_optional(value);
}

void Settings::setTranslateButtonEnabled(bool value) {
	_translateButtonEnabled = value;
}

bool Settings::translateButtonEnabled() const {
	return _translateButtonEnabled;
}

void Settings::setTranslateChatEnabled(bool value) {
	_translateChatEnabled = value;
}

bool Settings::translateChatEnabled() const {
	return _translateChatEnabled.current();
}

rpl::producer<bool> Settings::translateChatEnabledValue() const {
	return _translateChatEnabled.value();
}

[[nodiscard]] const std::vector<LanguageId> &DefaultSkipLanguages() {
	using namespace Platform;

	static auto Result = [&] {
		auto list = std::vector<LanguageId>();
		list.push_back({ LanguageId::FromName(Lang::Id()) });
		const auto systemId = LanguageId::FromName(SystemLanguage());
		if (list.back() != systemId) {
			list.push_back(systemId);
		}

		Ensures(!list.empty());
		return list;
	}();
	return Result;
}

[[nodiscard]] std::vector<LanguageId> NonEmptySkipList(
		std::vector<LanguageId> list) {
	return list.empty() ? DefaultSkipLanguages() : list;
}

void Settings::setTranslateTo(LanguageId id) {
	_translateToRaw = int(id.value);
}

LanguageId Settings::translateTo() const {
	if (const auto raw = _translateToRaw.current()) {
		return { QLocale::Language(raw) };
	}
	return DefaultSkipLanguages().front();
}

rpl::producer<LanguageId> Settings::translateToValue() const {
	return _translateToRaw.value() | rpl::map([=](int raw) {
		return raw
			? LanguageId{ QLocale::Language(raw) }
			: DefaultSkipLanguages().front();
	}) | rpl::distinct_until_changed();
}

void Settings::setSkipTranslationLanguages(
		std::vector<LanguageId> languages) {
	_skipTranslationLanguages = std::move(languages);
}

auto Settings::skipTranslationLanguages() const -> std::vector<LanguageId> {
	return NonEmptySkipList(_skipTranslationLanguages.current());
}

auto Settings::skipTranslationLanguagesValue() const
-> rpl::producer<std::vector<LanguageId>> {
	return _skipTranslationLanguages.value() | rpl::map(NonEmptySkipList);
}

void Settings::setRememberedDeleteMessageOnlyForYou(bool value) {
	_rememberedDeleteMessageOnlyForYou = value;
}
bool Settings::rememberedDeleteMessageOnlyForYou() const {
	return _rememberedDeleteMessageOnlyForYou;
}

} // namespace Core
