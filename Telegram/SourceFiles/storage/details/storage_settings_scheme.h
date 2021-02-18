/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_dc_options.h"
#include "main/main_session_settings.h"
#include "storage/storage_account.h"

namespace MTP {
class AuthKey;
} // namespace MTP

namespace Storage {
namespace details {

struct ReadSettingsContext {
	[[nodiscard]] Main::SessionSettings &sessionSettings() {
		if (!sessionSettingsStorage) {
			sessionSettingsStorage
				= std::make_unique<Main::SessionSettings>();
		}
		return *sessionSettingsStorage;
	}

	// This field is read in ReadSetting.
	bool legacyHasCustomDayBackground = false;

	// Those fields are written in ReadSetting.
	MTP::DcOptions fallbackConfigLegacyDcOptions
		= MTP::DcOptions(MTP::Environment::Production);
	qint32 fallbackConfigLegacyChatSizeMax = 0;
	qint32 fallbackConfigLegacySavedGifsLimit = 0;
	qint32 fallbackConfigLegacyStickersRecentLimit = 0;
	qint32 fallbackConfigLegacyStickersFavedLimit = 0;
	qint32 fallbackConfigLegacyMegagroupSizeMax = 0;
	QString fallbackConfigLegacyTxtDomainString;
	QByteArray fallbackConfig;

	qint64 cacheTotalSizeLimit = 0;
	qint32 cacheTotalTimeLimit = 0;
	qint64 cacheBigFileTotalSizeLimit = 0;
	qint32 cacheBigFileTotalTimeLimit = 0;

	std::unique_ptr<Main::SessionSettings> sessionSettingsStorage;

	FileKey themeKeyLegacy = 0;
	FileKey themeKeyDay = 0;
	FileKey themeKeyNight = 0;
	FileKey backgroundKeyDay = 0;
	FileKey backgroundKeyNight = 0;
	bool backgroundKeysRead = false;
	bool tileDay = false;
	bool tileNight = true;
	bool tileRead = false;
	FileKey langPackKey = 0;
	FileKey languagesKey = 0;

	QByteArray mtpAuthorization;
	std::vector<std::shared_ptr<MTP::AuthKey>> mtpLegacyKeys;
	qint32 mtpLegacyMainDcId = 0;
	qint32 mtpLegacyUserId = 0;

	bool legacyRead = false;
};

[[nodiscard]] bool ReadSetting(
	quint32 blockId,
	QDataStream &stream,
	int version,
	ReadSettingsContext &context);

void ApplyReadFallbackConfig(ReadSettingsContext &context);

enum {
	dbiKey = 0x00,
	dbiUser = 0x01,
	dbiDcOptionOldOld = 0x02,
	dbiChatSizeMaxOld = 0x03,
	dbiMutePeerOld = 0x04,
	dbiSendKeyOld = 0x05,
	dbiAutoStart = 0x06,
	dbiStartMinimized = 0x07,
	dbiSoundFlashBounceNotifyOld = 0x08,
	dbiWorkMode = 0x09,
	dbiSeenTrayTooltip = 0x0a,
	dbiDesktopNotifyOld = 0x0b,
	dbiAutoUpdate = 0x0c,
	dbiLastUpdateCheck = 0x0d,
	dbiWindowPositionOld = 0x0e,
	dbiConnectionTypeOld = 0x0f,
	// 0x10 reserved
	dbiDefaultAttach = 0x11,
	dbiCatsAndDogsOld = 0x12,
	dbiReplaceEmojiOld = 0x13,
	dbiAskDownloadPathOld = 0x14,
	dbiDownloadPathOldOld = 0x15,
	dbiScaleOld = 0x16,
	dbiEmojiTabOld = 0x17,
	dbiRecentEmojiOldOld = 0x18,
	dbiLoggedPhoneNumberOld = 0x19,
	dbiMutedPeersOld = 0x1a,
	// 0x1b reserved
	dbiNotifyViewOld = 0x1c,
	dbiSendToMenu = 0x1d,
	dbiCompressPastedImageOld = 0x1e,
	dbiLangOld = 0x1f,
	dbiLangFileOld = 0x20,
	dbiTileBackgroundOld = 0x21,
	dbiAutoLockOld = 0x22,
	dbiDialogLastPath = 0x23,
	dbiRecentEmojiOld = 0x24,
	dbiEmojiVariantsOld = 0x25,
	dbiRecentStickers = 0x26,
	dbiDcOptionOld = 0x27,
	dbiTryIPv6 = 0x28,
	dbiSongVolumeOld = 0x29,
	dbiWindowsNotificationsOld = 0x30,
	dbiIncludeMutedOld = 0x31,
	dbiMegagroupSizeMaxOld = 0x32,
	dbiDownloadPathOld = 0x33,
	dbiAutoDownloadOld = 0x34,
	dbiSavedGifsLimitOld = 0x35,
	dbiShowingSavedGifsOld = 0x36,
	dbiAutoPlayOld = 0x37,
	dbiAdaptiveForWideOld = 0x38,
	dbiHiddenPinnedMessagesOld = 0x39,
	dbiRecentEmoji = 0x3a,
	dbiEmojiVariants = 0x3b,
	dbiDialogsModeOld = 0x40,
	dbiModerateModeOld = 0x41,
	dbiVideoVolumeOld = 0x42,
	dbiStickersRecentLimitOld = 0x43,
	dbiNativeNotificationsOld = 0x44,
	dbiNotificationsCountOld = 0x45,
	dbiNotificationsCornerOld = 0x46,
	dbiThemeKeyOld = 0x47,
	dbiDialogsWidthRatioOld = 0x48,
	dbiUseExternalVideoPlayer = 0x49,
	dbiDcOptionsOld = 0x4a,
	dbiMtpAuthorization = 0x4b,
	dbiLastSeenWarningSeenOld = 0x4c,
	dbiSessionSettings = 0x4d,
	dbiLangPackKey = 0x4e,
	dbiConnectionType = 0x4f,
	dbiStickersFavedLimitOld = 0x50,
	dbiSuggestStickersByEmojiOld = 0x51,
	dbiSuggestEmojiOld = 0x52,
	dbiTxtDomainStringOldOld = 0x53,
	dbiThemeKey = 0x54,
	dbiTileBackground = 0x55,
	dbiCacheSettingsOld = 0x56,
	dbiAnimationsDisabled = 0x57,
	dbiScalePercent = 0x58,
	dbiPlaybackSpeedOld = 0x59,
	dbiLanguagesKey = 0x5a,
	dbiCallSettingsOld = 0x5b,
	dbiCacheSettings = 0x5c,
	dbiTxtDomainStringOld = 0x5d,
	dbiApplicationSettings = 0x5e,
	dbiDialogsFiltersOld = 0x5f,
	dbiFallbackProductionConfig = 0x60,
	dbiBackgroundKey = 0x61,

	dbiEncryptedWithSalt = 333,
	dbiEncrypted = 444,

	// 500-600 reserved

	dbiVersion = 666,
};

enum {
	dbictAuto = 0,
	dbictHttpAuto = 1, // not used
	dbictHttpProxy = 2,
	dbictTcpProxy = 3,
	dbictProxiesListOld = 4,
	dbictProxiesList = 5,
};

inline constexpr auto kProxyTypeShift = 1024;

} // namespace details
} // namespace Storage
