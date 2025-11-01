/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings.h"

#include "ui/emoji_config.h"

Qt::LayoutDirection gLangDir = Qt::LeftToRight;

bool gInstallBetaVersion = AppBetaVersion;
uint64 gAlphaVersion = AppAlphaVersion;
uint64 gRealAlphaVersion = AppAlphaVersion;
QByteArray gAlphaPrivateKey;

bool gManyInstance = false;
QString gKeyFile;
QString gWorkingDir;

QList<QUrl> gStartUrls;

QString gDialogLastPath;
QString gDialogHelperPath;

bool gStartMinimized = false;
bool gStartInTray = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gAutoUpdate = true;
LaunchMode gLaunchMode = LaunchModeNormal;
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false;
bool gRestarting = false;
bool gRestartingToSettings = false;
bool gWriteProtected = false;
bool gQuit = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;
bool gDebugMode = false;

uint32 gConnectionsInSession = 1;

QByteArray gLocalSalt;
int gScreenScale = style::kScaleAuto;
int gConfigScale = style::kScaleAuto;

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;

RecentHashtagPack gRecentWriteHashtags;
RecentHashtagPack gRecentSearchHashtags;

RecentInlineBots gRecentInlineBots;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
crl::time gPasscodeLastTry = 0;

float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;

int gOtherOnline = 0;

struct AutoDownloadSettings {
	int32 photo = 0;
	int32 audio = 0;
	int32 gif = 0;
};

AutoDownloadSettings gAutoDownload;
