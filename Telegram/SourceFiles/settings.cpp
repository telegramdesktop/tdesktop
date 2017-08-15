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
#include "settings.h"

#include "platform/platform_specific.h"

bool gRtl = false;
Qt::LayoutDirection gLangDir = gRtl ? Qt::RightToLeft : Qt::LeftToRight;

QString gArguments;

bool gAlphaVersion = AppAlphaVersion;
uint64 gBetaVersion = AppBetaVersion;
uint64 gRealBetaVersion = AppBetaVersion;
QByteArray gBetaPrivateKey;

bool gTestMode = false;
bool gDebug = false;
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
bool gAutoUpdate = true;
TWindowPos gWindowPos;
LaunchMode gLaunchMode = LaunchModeNormal;
bool gSupportTray = true;
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false, gRestarting = false, gRestartingToSettings = false, gWriteProtected = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;
bool gReplaceEmojis = true;

bool gCtrlEnter = false;

uint32 gConnectionsInSession = 1;
QString gLoggedPhoneNumber;

QByteArray gLocalSalt;
DBIScale gRealScale = dbisAuto, gScreenScale = dbisOne, gConfigScale = dbisAuto;
bool gCompressPastedImage = true;

QString gTimeFormat = qsl("hh:mm");

RecentEmojiPack gRecentEmoji;
RecentEmojiPreload gRecentEmojiPreload;
EmojiColorVariants gEmojiVariants;

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;

SavedGifs gSavedGifs;
TimeMs gLastSavedGifsUpdate = 0;

RecentHashtagPack gRecentWriteHashtags, gRecentSearchHashtags;

RecentInlineBots gRecentInlineBots;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
TimeMs gPasscodeLastTry = 0;

bool gRetina = false;
float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;

#ifdef Q_OS_WIN
DBIPlatform gPlatform = dbipWindows;
#elif defined Q_OS_MAC
DBIPlatform gPlatform = dbipMac;
#elif defined Q_OS_LINUX64
DBIPlatform gPlatform = dbipLinux64;
#elif defined Q_OS_LINUX32
DBIPlatform gPlatform = dbipLinux32;
#else
#error Unknown platform
#endif
QString gPlatformString;
QUrl gUpdateURL;
bool gIsElCapitan = false;

int gOtherOnline = 0;

SavedPeers gSavedPeers;
SavedPeersByTime gSavedPeersByTime;

ReportSpamStatuses gReportSpamStatuses;

int32 gAutoDownloadPhoto = 0; // all auto download
int32 gAutoDownloadAudio = 0;
int32 gAutoDownloadGif = 0;
bool gAutoPlayGif = true;

void settingsParseArgs(int argc, char *argv[]) {
#ifdef Q_OS_MAC
#ifndef OS_MAC_OLD
	if (QSysInfo::macVersion() >= QSysInfo::MV_10_11) {
		gIsElCapitan = true;
	}
#else // OS_MAC_OLD
	gPlatform = dbipMacOld;
#endif // OS_MAC_OLD
#endif // Q_OS_MAC

	switch (cPlatform()) {
	case dbipWindows:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/win/tupdates/current"));
#ifndef OS_WIN_STORE
		gPlatformString = qsl("Windows");
#else // OS_WIN_STORE
		gPlatformString = qsl("WinStore");
#endif // OS_WIN_STORE
	break;
	case dbipMac:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/mac/tupdates/current"));
#ifndef OS_MAC_STORE
		gPlatformString = qsl("MacOS");
#else // OS_MAC_STORE
		gPlatformString = qsl("MacAppStore");
#endif // OS_MAC_STORE
	break;
	case dbipMacOld:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/mac32/tupdates/current"));
		gPlatformString = qsl("MacOSold");
	break;
	case dbipLinux64:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/linux/tupdates/current"));
		gPlatformString = qsl("Linux64bit");
	break;
	case dbipLinux32:
		gUpdateURL = QUrl(qsl("http://tdesktop.com/linux32/tupdates/current"));
		gPlatformString = qsl("Linux32bit");
	break;
	}

	QStringList args;
	for (int32 i = 0; i < argc; ++i) {
		args.push_back(fromUtf8Safe(argv[i]));
	}
	gArguments = args.join(' ');

	auto path = Platform::CurrentExecutablePath(argc, argv);
	LOG(("Executable path before check: %1").arg(path));
	if (!path.isEmpty()) {
		auto info = QFileInfo(path);
		if (info.isSymLink()) {
			info = info.symLinkTarget();
		}
		if (info.exists()) {
			gExeDir = info.absoluteDir().absolutePath() + '/';
			gExeName = info.fileName();
		}
	}
	if (cExeName().isEmpty()) {
		LOG(("WARNING: Could not compute executable path, some features will be disabled."));
	}
	for (auto i = 0; i != argc; ++i) {
		if (qstr("-testmode") == argv[i]) {
			gTestMode = true;
		} else if (qstr("-debug") == argv[i]) {
			gDebug = true;
		} else if (qstr("-many") == argv[i]) {
			gManyInstance = true;
		} else if (qstr("-key") == argv[i] && i + 1 < argc) {
			gKeyFile = fromUtf8Safe(argv[++i]);
		} else if (qstr("-autostart") == argv[i]) {
			gLaunchMode = LaunchModeAutoStart;
		} else if (qstr("-fixprevious") == argv[i]) {
			gLaunchMode = LaunchModeFixPrevious;
		} else if (qstr("-cleanup") == argv[i]) {
			gLaunchMode = LaunchModeCleanup;
		} else if (qstr("-noupdate") == argv[i]) {
			gNoStartUpdate = true;
		} else if (qstr("-tosettings") == argv[i]) {
			gStartToSettings = true;
		} else if (qstr("-startintray") == argv[i]) {
			gStartInTray = true;
		} else if (qstr("-sendpath") == argv[i] && i + 1 < argc) {
			for (++i; i < argc; ++i) {
				gSendPaths.push_back(fromUtf8Safe(argv[i]));
			}
		} else if (qstr("-workdir") == argv[i] && i + 1 < argc) {
			QString dir = fromUtf8Safe(argv[++i]);
			if (QDir().exists(dir)) {
				gWorkingDir = dir;
			}
		} else if (qstr("--") == argv[i] && i + 1 < argc) {
			gStartUrl = fromUtf8Safe(argv[++i]).mid(0, 8192);
		}
	}
}

RecentStickerPack &cGetRecentStickers() {
	if (cRecentStickers().isEmpty() && !cRecentStickersPreload().isEmpty()) {
		RecentStickerPreload p(cRecentStickersPreload());
		cSetRecentStickersPreload(RecentStickerPreload());

		RecentStickerPack &recent(cRefRecentStickers());
		recent.reserve(p.size());
		for (RecentStickerPreload::const_iterator i = p.cbegin(), e = p.cend(); i != e; ++i) {
			DocumentData *doc = App::document(i->first);
			if (!doc || !doc->sticker()) continue;

			recent.push_back(qMakePair(doc, i->second));
		}
	}
	return cRefRecentStickers();
}
