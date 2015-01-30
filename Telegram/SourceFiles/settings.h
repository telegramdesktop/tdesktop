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
#pragma once

extern bool gDebug;
inline bool cDebug() {
#if defined _DEBUG
	return true;
#elif defined _WITH_DEBUG
	return gDebug;
#else
	return false;
#endif
}
inline void cSetDebug(bool debug) {
	gDebug = debug;
}

#define DeclareReadSetting(Type, Name) extern Type g##Name; \
inline const Type &c##Name() { \
	return g##Name; \
}

#define DeclareSetting(Type, Name) DeclareReadSetting(Type, Name) \
inline void cSet##Name(const Type &Name) { \
	g##Name = Name; \
}

DeclareSetting(bool, TestMode);
DeclareSetting(QString, LoggedPhoneNumber);
DeclareReadSetting(uint32, ConnectionsInSession);
DeclareSetting(bool, AutoStart);
DeclareSetting(bool, StartMinimized);
DeclareSetting(bool, StartInTray);
DeclareSetting(bool, SendToMenu);
DeclareReadSetting(bool, FromAutoStart);
DeclareSetting(QString, WorkingDir);
inline void cForceWorkingDir(const QString &newDir) {
	cSetWorkingDir(newDir);
	QDir dir;
	if (!gWorkingDir.isEmpty()) dir.mkpath(gWorkingDir);
}
DeclareReadSetting(QString, ExeName);
DeclareReadSetting(QString, ExeDir);
DeclareSetting(QString, DialogLastPath);
DeclareSetting(QString, DialogHelperPath);
inline const QString &cDialogHelperPathFinal() {
	return cDialogHelperPath().isEmpty() ? cExeDir() : cDialogHelperPath();
}
DeclareSetting(bool, CtrlEnter);
DeclareSetting(bool, CatsAndDogs);
DeclareSetting(bool, SoundNotify);
DeclareSetting(bool, NeedConfigResave);
DeclareSetting(bool, DesktopNotify);
DeclareSetting(DBINotifyView, NotifyView);
DeclareSetting(bool, AutoUpdate);

struct TWindowPos {
	TWindowPos() : moncrc(0), maximized(0), x(0), y(0), w(0), h(0) {
	}
	int32 moncrc, maximized;
	int32 x, y, w, h;
};
DeclareSetting(TWindowPos, WindowPos);
DeclareSetting(bool, SupportTray);
DeclareSetting(DBIWorkMode, WorkMode);
DeclareSetting(DBIConnectionType, ConnectionType);
DeclareSetting(DBIDefaultAttach, DefaultAttach);
DeclareSetting(ConnectionProxy, ConnectionProxy);
DeclareSetting(bool, SeenTrayTooltip);
DeclareSetting(bool, RestartingUpdate);
DeclareSetting(bool, Restarting);
DeclareSetting(bool, RestartingToSettings);
DeclareSetting(bool, WriteProtected);
DeclareSetting(int32, LastUpdateCheck);
DeclareSetting(bool, NoStartUpdate);
DeclareSetting(bool, StartToSettings);
DeclareSetting(int32, MaxGroupCount);
DeclareSetting(bool, ReplaceEmojis);
DeclareReadSetting(bool, ManyInstance);
DeclareSetting(bool, AskDownloadPath);
DeclareSetting(QString, DownloadPath);
DeclareSetting(QByteArray, LocalSalt);
DeclareSetting(DBIScale, RealScale);
DeclareSetting(DBIScale, ScreenScale);
DeclareSetting(DBIScale, ConfigScale);
DeclareSetting(bool, CompressPastedImage);

inline DBIScale cEvalScale(DBIScale scale) {
	return (scale == dbisAuto) ? cScreenScale() : scale;
}
inline DBIScale cScale() {
	return cEvalScale(cRealScale());
}

template <typename T>
T convertScale(T v) {
	switch (cScale()) {
		case dbisOneAndQuarter: return qRound(float64(v) * 1.25 - 0.01);
		case dbisOneAndHalf: return qRound(float64(v) * 1.5 - 0.01);
		case dbisTwo: return v * 2;
	}
	return v;
}

DeclareSetting(DBIEmojiTab, EmojiTab);

struct EmojiData {
	EmojiData(uint16 x, uint16 y, uint32 code, uint32 code2, uint16 len, uint16 postfix = 0) : x(x), y(y), code(code), code2(code2), len(len), postfix(postfix) {
	}
	uint16 x, y;
	uint32 code, code2;
	uint16 len;
	uint16 postfix;
};

typedef const EmojiData *EmojiPtr;

typedef QVector<EmojiPtr> EmojiPack;
typedef QVector<QPair<uint32, ushort> > RecentEmojiPreload;
typedef QVector<QPair<EmojiPtr, ushort> > RecentEmojiPack;
DeclareSetting(RecentEmojiPack, RecentEmojis);
DeclareSetting(RecentEmojiPreload, RecentEmojisPreload);

const RecentEmojiPack &cGetRecentEmojis();

struct DocumentData;
typedef QVector<DocumentData*> StickerPack;
typedef QMap<EmojiPtr, StickerPack> AllStickers;
DeclareSetting(AllStickers, Stickers);
DeclareSetting(QByteArray, StickersHash);

typedef QMap<DocumentData*, EmojiPtr> EmojiStickersMap;
DeclareSetting(EmojiStickersMap, EmojiStickers);

typedef QList<QPair<DocumentData*, int16> > RecentStickerPack;
DeclareSetting(RecentStickerPack, RecentStickers);

DeclareSetting(int32, Lang);
DeclareSetting(QString, LangFile);

DeclareSetting(QStringList, SendPaths);
DeclareSetting(QString, StartUrl);

DeclareSetting(QString, LangErrors);

DeclareSetting(bool, Retina);
DeclareSetting(float64, RetinaFactor);
DeclareSetting(int32, IntRetinaFactor);
DeclareSetting(bool, CustomNotifies);

DeclareReadSetting(uint64, Instance);

DeclareReadSetting(DBIPlatform, Platform);
DeclareReadSetting(QUrl, UpdateURL);

DeclareSetting(bool, ContactsReceived);

DeclareSetting(bool, WideMode);

DeclareSetting(int, OnlineUpdatePeriod);
DeclareSetting(int, OfflineBlurTimeout);
DeclareSetting(int, OfflineIdleTimeout);
DeclareSetting(int, OnlineFocusTimeout);
DeclareSetting(int, OnlineCloudTimeout);
DeclareSetting(int, NotifyCloudDelay);
DeclareSetting(int, NotifyDefaultDelay);

DeclareSetting(int, OtherOnline);

void settingsParseArgs(int argc, char *argv[]);
