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

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gSoundNotify = true;
bool gDesktopNotify = true;
bool gStartMinimized = false;
bool gAutoStart = false;
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

DBIEmojiTab gEmojiTab = dbietPeople;
RecentEmojiPack gRecentEmojis;
RecentEmojiPreload gRecentEmojisPreload;

QString gLangFile;

void settingsParseArgs(int argc, char *argv[]) {
	gExeDir = psCurrentExeDirectory();
	for (uint32 i = 0; i < argc; ++i) {
		if (string("-release") == argv[i]) {
			gTestMode = false;
		} else if (string("-debug") == argv[i]) {
			gDebug = true;
		} else if (string("-many") == argv[i]) {
			gManyInstance = true;
		} else if (string("-key") == argv[i] && i + 1 < argc) {
			gKeyFile = QString(argv[i + 1]);
		} else if (string("-autostart") == argv[i]) {
			gFromAutoStart = true;
		} else if (string("-noupdate") == argv[i]) {
			gNoStartUpdate = true;
		} else if (string("-tosettings") == argv[i]) {
			gStartToSettings = true;
		} else if (string("-lang") == argv[i] && i + 1 < argc) {
			gLangFile = QString(argv[i + 1]);
		}
	}
}

const RecentEmojiPack &cGetRecentEmojis() {
	if (cRecentEmojis().isEmpty() && !cRecentEmojisPreload().isEmpty()) {
		RecentEmojiPreload p(cRecentEmojisPreload());
		cSetRecentEmojisPreload(RecentEmojiPreload());
		RecentEmojiPack r;
		r.reserve(p.size());
		for (RecentEmojiPreload::const_iterator i = p.cbegin(), e = p.cend(); i != e; ++i) {
			EmojiPtr ep(getEmoji(i->first));
			if (ep) {
				r.push_back(qMakePair(ep, i->second));
			}
		}
		cSetRecentEmojis(r);
	}
	return cRecentEmojis();
}
