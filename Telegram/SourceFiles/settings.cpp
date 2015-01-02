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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "pspecific.h"
#include "settings.h"
#include "lang.h"

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
bool gDesktopNotify = true;
DBINotifyView gNotifyView = dbinvShowPreview;
bool gStartMinimized = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gAutoUpdate = true;
TWindowPos gWindowPos;
bool gFromAutoStart = false;
DBIWorkMode gWorkMode = dbiwmWindowAndTray;
DBIConnectionType gConnectionType = dbictAuto;
ConnectionProxy gConnectionProxy;
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
bool gCatsAndDogs = true;

uint32 gConnectionsInSession = 1;
QString gLoggedPhoneNumber;

QByteArray gLocalSalt;
DBIScale gRealScale = dbisAuto, gScreenScale = dbisOne, gConfigScale = dbisAuto;
bool gCompressPastedImage = true;

DBIEmojiTab gEmojiTab = dbietRecent;
RecentEmojiPack gRecentEmojis;
RecentEmojiPreload gRecentEmojisPreload;

AllStickers gStickers;
QByteArray gStickersHash;
RecentStickerPack gRecentStickers;

int32 gLang = -2; // auto
QString gLangFile;

bool gRetina = false;
float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;
#ifdef Q_OS_MAC
bool gCustomNotifies = false;
#else
bool gCustomNotifies = true;
#endif
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

bool gWideMode = true;

void settingsParseArgs(int argc, char *argv[]) {
	if (cPlatform() == dbipMac) {
		gCustomNotifies = false;
	} else {
		gCustomNotifies = true;
	}
    memset_rand(&gInstance, sizeof(gInstance));
	gExeDir = psCurrentExeDirectory(argc, argv);
	gExeName = psCurrentExeName(argc, argv);
    for (int32 i = 0; i < argc; ++i) {
		if (string("-release") == argv[i]) {
			gTestMode = false;
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

const RecentEmojiPack &cGetRecentEmojis() {
	if (cRecentEmojis().isEmpty()) {
		RecentEmojiPack r;
		if (false && !cRecentEmojisPreload().isEmpty()) {
			RecentEmojiPreload p(cRecentEmojisPreload());
			cSetRecentEmojisPreload(RecentEmojiPreload());
			r.reserve(p.size());
			for (RecentEmojiPreload::const_iterator i = p.cbegin(), e = p.cend(); i != e; ++i) {
				uint32 code = ((i->first & 0xFFFFU) == 0xFE0FU) ? ((i->first >> 16) & 0xFFFFU) : i->first;
				EmojiPtr ep(getEmoji(code));
				if (!ep) continue;

				if (ep->postfix) {
					int32 j = 0, l = r.size();
					for (; j < l; ++j) {
						if (r[j].first->code == code) {
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
		uint32 defaultRecent[] = {
			0xD83DDE02U,
			0xD83DDE18U,
			0x2764U,
			0xD83DDE0DU,
			0xD83DDE0AU,
			0xD83DDE01U,
			0xD83DDC4DU,
			0x263AU,
			0xD83DDE14U,
			0xD83DDE04U,
			0xD83DDE2DU,
			0xD83DDC8BU,
			0xD83DDE12U,
			0xD83DDE33U,
			0xD83DDE1CU,
			0xD83DDE48U,
			0xD83DDE09U,
			0xD83DDE03U,
			0xD83DDE22U,
			0xD83DDE1DU,
			0xD83DDE31U,
			0xD83DDE21U,
			0xD83DDE0FU,
			0xD83DDE1EU,
			0xD83DDE05U,
			0xD83DDE1AU,
			0xD83DDE4AU,
			0xD83DDE0CU,
			0xD83DDE00U,
			0xD83DDE0BU,
			0xD83DDE06U,
			0xD83DDC4CU,
			0xD83DDE10U,
			0xD83DDE15U,
		};
		for (int32 i = 0, s = sizeof(defaultRecent) / sizeof(defaultRecent[0]); i < s; ++i) {
			if (r.size() >= EmojiPadPerRow * EmojiPadRowsPerPage) break;

			EmojiPtr ep(getEmoji(defaultRecent[i]));
			if (!ep) continue;

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
	return cRecentEmojis();
}
