/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "pspecific.h"
#include "settings.h"

bool gTestMode = false;
bool gDebug = false;
bool gManyInstance = false;
QString gKeyFile;
QString gWorkingDir, gExeDir;

QStringList gSendPaths;

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gSoundNotify = true;
bool gDesktopNotify = true;
bool gFlashWindow = true;
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
bool gRestartingUpdate = false, gRestarting = false;
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

void settingsParseArgs(int argc, char *argv[]) {
	if (cPlatform() == dbipMac) {
		gCustomNotifies = false;
	} else {
		gCustomNotifies = true;
	}
    memset_rand(&gInstance, sizeof(gInstance));
	gExeDir = psCurrentExeDirectory(argc, argv);
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
		} else if (string("-lang") == argv[i] && i + 1 < argc) {
			gLangFile = QString(argv[++i]);
		} else if (string("-sendpath") == argv[i] && i + 1 < argc) {
			for (++i; i < argc; ++i) {
				gSendPaths.push_back(QString::fromLocal8Bit(argv[i]));
			}
		}
	}
}

const RecentEmojiPack &cGetRecentEmojis() {
	if (cRecentEmojis().isEmpty()) {
		RecentEmojiPack r;
		if (!cRecentEmojisPreload().isEmpty()) {
			RecentEmojiPreload p(cRecentEmojisPreload());
			cSetRecentEmojisPreload(RecentEmojiPreload());
			r.reserve(p.size());
			for (RecentEmojiPreload::const_iterator i = p.cbegin(), e = p.cend(); i != e; ++i) {
				EmojiPtr ep(getEmoji(i->first));
				if (ep) {
					r.push_back(qMakePair(ep, i->second));
				}
			}
		}
		uint32 defaultRecent[] = {
			0xD83DDE02,
			0xD83DDE18,
			0x2764,
			0xD83DDE0D,
			0xD83DDE0A,
			0xD83DDE01,
			0xD83DDC4D,
			0x263A,
			0xD83DDE14,
			0xD83DDE04,
			0xD83DDE2D,
			0xD83DDC8B,
			0xD83DDE12,
			0xD83DDE33,
			0xD83DDE1C,
			0xD83DDE48,
			0xD83DDE09,
			0xD83DDE03,
			0xD83DDE22,
			0xD83DDE1D,
			0xD83DDE31,
			0xD83DDE21,
			0xD83DDE0F,
			0xD83DDE1E,
			0xD83DDE05,
			0xD83DDE1A,
			0xD83DDE4A,
			0xD83DDE0C,
			0xD83DDE00,
			0xD83DDE0B,
			0xD83DDE06,
			0xD83DDC4C,
			0xD83DDE10,
			0xD83DDE15,
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
