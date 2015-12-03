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

#define DeclareRefSetting(Type, Name) DeclareSetting(Type, Name) \
inline Type &cRef##Name() { \
	return g##Name; \
}

DeclareSetting(bool, Rtl);
DeclareSetting(Qt::LayoutDirection, LangDir);
inline bool rtl() {
	return cRtl();
}

struct mtpDcOption {
	mtpDcOption(int id, int flags, const string &ip, int port) : id(id), flags(flags), ip(ip), port(port) {
	}

	int id;
	int flags;
	string ip;
	int port;
};
typedef QMap<int, mtpDcOption> mtpDcOptions;
DeclareSetting(mtpDcOptions, DcOptions);

DeclareSetting(bool, DevVersion);
DeclareSetting(uint64, BetaVersion);
DeclareSetting(uint64, RealBetaVersion);
DeclareSetting(QByteArray, BetaPrivateKey);

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
	if (!gWorkingDir.isEmpty()) QDir().mkpath(gWorkingDir);
}
DeclareReadSetting(QString, ExeName);
DeclareReadSetting(QString, ExeDir);
DeclareSetting(QString, DialogLastPath);
DeclareSetting(QString, DialogHelperPath);
inline const QString &cDialogHelperPathFinal() {
	return cDialogHelperPath().isEmpty() ? cExeDir() : cDialogHelperPath();
}
DeclareSetting(bool, CtrlEnter);

typedef QPixmap *QPixmapPointer;
DeclareSetting(QPixmapPointer, ChatBackground);
DeclareSetting(int32, ChatBackgroundId);
DeclareSetting(QPixmapPointer, ChatDogImage);
DeclareSetting(bool, TileBackground);

DeclareSetting(bool, SoundNotify);
DeclareSetting(bool, IncludeMuted);
DeclareSetting(bool, NeedConfigResave);
DeclareSetting(bool, DesktopNotify);
DeclareSetting(DBINotifyView, NotifyView);
DeclareSetting(bool, AutoUpdate);

DeclareSetting(bool, WindowsNotifications);

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
DeclareSetting(bool, TryIPv6);
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
DeclareSetting(int32, MaxMegaGroupCount);
DeclareSetting(bool, ReplaceEmojis);
DeclareReadSetting(bool, ManyInstance);
DeclareSetting(bool, AskDownloadPath);
DeclareSetting(QString, DownloadPath);
DeclareSetting(QByteArray, DownloadPathBookmark);
DeclareSetting(QByteArray, LocalSalt);
DeclareSetting(DBIScale, RealScale);
DeclareSetting(DBIScale, ScreenScale);
DeclareSetting(DBIScale, ConfigScale);
DeclareSetting(bool, CompressPastedImage);
DeclareSetting(QString, TimeFormat);

DeclareSetting(int32, AutoLock);
DeclareSetting(bool, HasPasscode);

DeclareSetting(bool, HasAudioPlayer);
DeclareSetting(bool, HasAudioCapture);

inline void cChangeTimeFormat(const QString &newFormat) {
	if (!newFormat.isEmpty()) cSetTimeFormat(newFormat);
}

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

struct EmojiData {
	EmojiData(uint16 x, uint16 y, uint32 code, uint32 code2, uint16 len, uint16 postfix, uint32 color) : x(x), y(y), code(code), code2(code2), len(len), postfix(postfix), color(color) {
	}
	uint16 x, y;
	uint32 code, code2;
	uint16 len;
	uint16 postfix;
	uint32 color;
};

typedef const EmojiData *EmojiPtr;
static EmojiPtr TwoSymbolEmoji = EmojiPtr(0x01);

typedef QVector<EmojiPtr> EmojiPack;
typedef QVector<QPair<uint32, ushort> > RecentEmojisPreloadOld;
typedef QVector<QPair<uint64, ushort> > RecentEmojisPreload;
typedef QVector<QPair<EmojiPtr, ushort> > RecentEmojiPack;
typedef QMap<uint32, uint64> EmojiColorVariants;
DeclareRefSetting(RecentEmojiPack, RecentEmojis);
DeclareSetting(RecentEmojisPreload, RecentEmojisPreload);
DeclareRefSetting(EmojiColorVariants, EmojiVariants);

RecentEmojiPack &cGetRecentEmojis();

struct DocumentData;
typedef QVector<DocumentData*> StickerPack;
DeclareSetting(int32, StickersHash);

typedef QList<QPair<DocumentData*, int16> > RecentStickerPackOld;
typedef QVector<QPair<uint64, ushort> > RecentStickerPreload;
typedef QVector<QPair<DocumentData*, ushort> > RecentStickerPack;
DeclareSetting(RecentStickerPreload, RecentStickersPreload);
DeclareRefSetting(RecentStickerPack, RecentStickers);

RecentStickerPack &cGetRecentStickers();

DeclareSetting(uint64, LastStickersUpdate);

static const uint64 DefaultStickerSetId = 0; // for backward compatibility
static const uint64 CustomStickerSetId = 0xFFFFFFFFFFFFFFFFULL, RecentStickerSetId = 0xFFFFFFFFFFFFFFFEULL;
static const uint64 NoneStickerSetId = 0xFFFFFFFFFFFFFFFDULL; // for emoji/stickers panel
struct StickerSet {
	StickerSet(uint64 id, uint64 access, const QString &title, const QString &shortName, int32 count, int32 hash, int32 flags) : id(id), access(access), title(title), shortName(shortName), count(count), hash(hash), flags(flags) {
	}
	uint64 id, access;
	QString title, shortName;
	int32 count, hash, flags;
	StickerPack stickers;
};
typedef QMap<uint64, StickerSet> StickerSets;
DeclareRefSetting(StickerSets, StickerSets);
typedef QList<uint64> StickerSetsOrder;
DeclareRefSetting(StickerSetsOrder, StickerSetsOrder);

typedef QList<QPair<QString, ushort> > RecentHashtagPack;
DeclareSetting(RecentHashtagPack, RecentWriteHashtags);
DeclareSetting(RecentHashtagPack, RecentSearchHashtags);

DeclareSetting(bool, PasswordRecovered);

DeclareSetting(int32, PasscodeBadTries);
DeclareSetting(uint64, PasscodeLastTry);

inline bool passcodeCanTry() {
	if (cPasscodeBadTries() < 3) return true;
	uint64 dt = getms(true) - cPasscodeLastTry();
	switch (cPasscodeBadTries()) {
	case 3: return dt >= 5000;
	case 4: return dt >= 10000;
	case 5: return dt >= 15000;
	case 6: return dt >= 20000;
	case 7: return dt >= 25000;
	}
	return dt >= 30000;
}

inline void incrementRecentHashtag(RecentHashtagPack &recent, const QString &tag) {
	RecentHashtagPack::iterator i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == tag) {
			++i->second;
		if (qAbs(i->second) > 0x4000) {
			for (RecentHashtagPack::iterator j = recent.begin(); j != e; ++j) {
				if (j->second > 1) {
					j->second /= 2;
				} else if (j->second > 0) {
					j->second = 1;
				}
			}
		}
			for (; i != recent.begin(); --i) {
				if (qAbs((i - 1)->second) > qAbs(i->second)) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= 64) recent.pop_back();
		recent.push_back(qMakePair(tag, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
}

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
DeclareReadSetting(bool, IsElCapitan);
DeclareReadSetting(QUrl, UpdateURL);

DeclareSetting(bool, ContactsReceived);
DeclareSetting(bool, DialogsReceived);

DeclareSetting(bool, WideMode);

DeclareSetting(int, OnlineUpdatePeriod);
DeclareSetting(int, OfflineBlurTimeout);
DeclareSetting(int, OfflineIdleTimeout);
DeclareSetting(int, OnlineFocusTimeout);
DeclareSetting(int, OnlineCloudTimeout);
DeclareSetting(int, NotifyCloudDelay);
DeclareSetting(int, NotifyDefaultDelay);

DeclareSetting(int, OtherOnline);

DeclareSetting(float64, SongVolume);

class PeerData;
typedef QMap<PeerData*, QDateTime> SavedPeers;
typedef QMultiMap<QDateTime, PeerData*> SavedPeersByTime;
DeclareRefSetting(SavedPeers, SavedPeers);
DeclareRefSetting(SavedPeersByTime, SavedPeersByTime);

typedef QMap<uint64, DBIPeerReportSpamStatus> ReportSpamStatuses;
DeclareRefSetting(ReportSpamStatuses, ReportSpamStatuses);

void settingsParseArgs(int argc, char *argv[]);
