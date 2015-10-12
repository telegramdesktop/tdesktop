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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "pspecific.h"
#include "settings.h"
#include "lang.h"

bool gRtl = false;
Qt::LayoutDirection gLangDir = gRtl ? Qt::RightToLeft : Qt::LeftToRight;

mtpDcOptions gDcOptions;

bool gDevVersion = DevVersion;
bool gTestMode = false;
bool gDebug = false;
bool gManyInstance = false;
QString gKeyFile;
QString gWorkingDir, gExeDir, gExeName;

QStringList gSendPaths;
QString gStartUrl;

QString gLangErrors;

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gSoundNotify = true;
bool gIncludeMuted = true;
bool gDesktopNotify = true;
DBINotifyView gNotifyView = dbinvShowPreview;
bool gWindowsNotifications = true;
bool gStartMinimized = false;
bool gStartInTray = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gAutoUpdate = true;
TWindowPos gWindowPos;
bool gFromAutoStart = false;
bool gSupportTray = true;
DBIWorkMode gWorkMode = dbiwmWindowAndTray;
DBIConnectionType gConnectionType = dbictAuto;
ConnectionProxy gConnectionProxy;
#ifdef Q_OS_WIN
bool gTryIPv6 = false;
#else
bool gTryIPv6 = true;
#endif
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false, gRestarting = false, gRestartingToSettings = false, gWriteProtected = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;
int32 gMaxGroupCount = 200;
DBIDefaultAttach gDefaultAttach = dbidaDocument;
bool gReplaceEmojis = true;
bool gAskDownloadPath = false;
QString gDownloadPath;

bool gNeedConfigResave = false;

bool gCtrlEnter = false;

QPixmapPointer gChatBackground = 0;
int32 gChatBackgroundId = 0;
QPixmapPointer gChatDogImage = 0;
bool gTileBackground = false;

uint32 gConnectionsInSession = 1;
QString gLoggedPhoneNumber;

QByteArray gLocalSalt;
DBIScale gRealScale = dbisAuto, gScreenScale = dbisOne, gConfigScale = dbisAuto;
bool gCompressPastedImage = true;

QString gTimeFormat = qsl("hh:mm");

int32 gAutoLock = 3600;
bool gHasPasscode = false;

bool gHasAudioPlayer = true;
bool gHasAudioCapture = true;

DBIEmojiTab gEmojiTab = dbietRecent;
RecentEmojiPack gRecentEmojis;
RecentEmojisPreload gRecentEmojisPreload;
EmojiColorVariants gEmojiVariants;

QByteArray gStickersHash;

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;
StickerSets gStickerSets;
StickerSetsOrder gStickerSetsOrder;

uint64 gLastStickersUpdate = 0;

RecentHashtagPack gRecentWriteHashtags, gRecentSearchHashtags;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
uint64 gPasscodeLastTry = 0;

int32 gLang = -2; // auto
QString gLangFile;

bool gRetina = false;
float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;
bool gCustomNotifies = true;
uint64 gInstance = 0.;

#ifdef Q_OS_WIN
DBIPlatform gPlatform = dbipWindows;
QUrl gUpdateURL = QUrl(qsl("http://tdesktop.com/win/tupdates/current"));
#elif defined Q_OS_MAC
DBIPlatform gPlatform = dbipMac;
QUrl gUpdateURL = QUrl(qsl("http://tdesktop.com/mac/tupdates/current"));
#elif defined Q_OS_LINUX32
DBIPlatform gPlatform = dbipLinux32;
QUrl gUpdateURL = QUrl(qsl("http://tdesktop.com/linux32/tupdates/current"));
#elif defined Q_OS_LINUX64
DBIPlatform gPlatform = dbipLinux64;
QUrl gUpdateURL = QUrl(qsl("http://tdesktop.com/linux/tupdates/current"));
#else
#error Unknown platform
#endif

bool gContactsReceived = false;
bool gDialogsReceived = false;

bool gWideMode = true;

int gOnlineUpdatePeriod = 120000;
int gOfflineBlurTimeout = 5000;
int gOfflineIdleTimeout = 30000;
int gOnlineFocusTimeout = 1000;
int gOnlineCloudTimeout = 300000;
int gNotifyCloudDelay = 30000;
int gNotifyDefaultDelay = 1500;

int gOtherOnline = 0;

float64 gSongVolume = 0.9;

SavedPeers gSavedPeers;
SavedPeersByTime gSavedPeersByTime;

ReportSpamStatuses gReportSpamStatuses;

void settingsParseArgs(int argc, char *argv[]) {
#ifdef Q_OS_MAC
	if (QSysInfo::macVersion() < QSysInfo::MV_10_8) {
		gUpdateURL = QUrl(qsl("http://tdesktop.com/mac32/tupdates/current"));
	} else {
		gCustomNotifies = false;
	}
#endif
    memset_rand(&gInstance, sizeof(gInstance));
	gExeDir = psCurrentExeDirectory(argc, argv);
	gExeName = psCurrentExeName(argc, argv);
    for (int32 i = 0; i < argc; ++i) {
		if (string("-testmode") == argv[i]) {
			gTestMode = true;
		} else if (string("-debug") == argv[i]) {
			gDebug = true;
		} else if (string("-many") == argv[i]) {
			gManyInstance = true;
		} else if (string("-key") == argv[i] && i + 1 < argc) {
			gKeyFile = QString::fromLocal8Bit(argv[++i]);
		} else if (string("-autostart") == argv[i]) {
			gFromAutoStart = true;
		} else if (string("-noupdate") == argv[i]) {
			gNoStartUpdate = true;
		} else if (string("-tosettings") == argv[i]) {
			gStartToSettings = true;
		} else if (string("-startintray") == argv[i]) {
			gStartInTray = true;
		} else if (string("-sendpath") == argv[i] && i + 1 < argc) {
			for (++i; i < argc; ++i) {
				gSendPaths.push_back(QString::fromLocal8Bit(argv[i]));
			}
		} else if (string("-workdir") == argv[i] && i + 1 < argc) {
			QString dir = QString::fromLocal8Bit(argv[++i]);
			if (QDir().exists(dir)) {
				gWorkingDir = dir;
			}
		} else if (string("--") == argv[i] && i + 1 < argc) {
			gStartUrl = QString::fromLocal8Bit(argv[++i]);
		}
	}
}

RecentEmojiPack &cGetRecentEmojis() {
	if (cRecentEmojis().isEmpty()) {
		RecentEmojiPack r;
		if (!cRecentEmojisPreload().isEmpty()) {
			RecentEmojisPreload p(cRecentEmojisPreload());
			cSetRecentEmojisPreload(RecentEmojisPreload());
			r.reserve(p.size());
			for (RecentEmojisPreload::const_iterator i = p.cbegin(), e = p.cend(); i != e; ++i) {
				uint64 code = ((!(i->first & 0xFFFFFFFF00000000LLU) && (i->first & 0xFFFFU) == 0xFE0FU)) ? ((i->first >> 16) & 0xFFFFU) : i->first;
				EmojiPtr ep(emojiFromKey(code));
				if (!ep) continue;

				if (ep->postfix) {
					int32 j = 0, l = r.size();
					for (; j < l; ++j) {
						if (emojiKey(r[j].first) == code) {
							break;
						}
					}
					if (j < l) {
						continue;
					}
				}
				r.push_back(qMakePair(ep, i->second));
			}
		}
		uint64 defaultRecent[] = {
			0xD83DDE02LLU,
			0xD83DDE18LLU,
			0x2764LLU,
			0xD83DDE0DLLU,
			0xD83DDE0ALLU,
			0xD83DDE01LLU,
			0xD83DDC4DLLU,
			0x263ALLU,
			0xD83DDE14LLU,
			0xD83DDE04LLU,
			0xD83DDE2DLLU,
			0xD83DDC8BLLU,
			0xD83DDE12LLU,
			0xD83DDE33LLU,
			0xD83DDE1CLLU,
			0xD83DDE48LLU,
			0xD83DDE09LLU,
			0xD83DDE03LLU,
			0xD83DDE22LLU,
			0xD83DDE1DLLU,
			0xD83DDE31LLU,
			0xD83DDE21LLU,
			0xD83DDE0FLLU,
			0xD83DDE1ELLU,
			0xD83DDE05LLU,
			0xD83DDE1ALLU,
			0xD83DDE4ALLU,
			0xD83DDE0CLLU,
			0xD83DDE00LLU,
			0xD83DDE0BLLU,
			0xD83DDE06LLU,
			0xD83DDC4CLLU,
			0xD83DDE10LLU,
			0xD83DDE15LLU,
		};
		for (int32 i = 0, s = sizeof(defaultRecent) / sizeof(defaultRecent[0]); i < s; ++i) {
			if (r.size() >= EmojiPanPerRow * EmojiPanRowsPerPage) break;

			EmojiPtr ep(emojiGet(defaultRecent[i]));
			if (!ep || ep == TwoSymbolEmoji) continue;

			int32 j = 0, l = r.size();
			for (; j < l; ++j) {
				if (r[j].first == ep) {
					break;
				}
			}
			if (j < l) continue;

			r.push_back(qMakePair(ep, 1));
		}
		cSetRecentEmojis(r);
	}
	return cRefRecentEmojis();
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
