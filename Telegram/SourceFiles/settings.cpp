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
QString gWorkingDir, gExeDir, gExeName;

QStringList gSendPaths;
QString gStartUrl;

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gStartMinimized = false;
bool gStartInTray = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gUseExternalVideoPlayer = false;
bool gUseFreeType = false;
bool gAutoUpdate = true;
LaunchMode gLaunchMode = LaunchModeNormal;
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false, gRestarting = false, gRestartingToSettings = false, gWriteProtected = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;
bool gDebugMode = false;

uint32 gConnectionsInSession = 1;

QByteArray gLocalSalt;
int gScreenScale = style::kScaleAuto;
int gConfigScale = style::kScaleAuto;

QString gTimeFormat = qsl("hh:mm");

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;

RecentHashtagPack gRecentWriteHashtags, gRecentSearchHashtags;

RecentInlineBots gRecentInlineBots;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
crl::time gPasscodeLastTry = 0;

float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;

int gOtherOnline = 0;

int32 gAutoDownloadPhoto = 0; // all auto download
int32 gAutoDownloadAudio = 0;
int32 gAutoDownloadGif = 0;
