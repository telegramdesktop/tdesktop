/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

DeclareSetting(bool, InstallBetaVersion);
DeclareSetting(uint64, AlphaVersion);
DeclareSetting(uint64, RealAlphaVersion);
DeclareSetting(QByteArray, AlphaPrivateKey);

DeclareSetting(bool, TestMode);
DeclareSetting(QString, LoggedPhoneNumber);
DeclareSetting(bool, AutoStart);
DeclareSetting(bool, StartMinimized);
DeclareSetting(bool, StartInTray);
DeclareSetting(bool, SendToMenu);
DeclareSetting(bool, UseExternalVideoPlayer);
enum LaunchMode {
	LaunchModeNormal = 0,
	LaunchModeAutoStart,
	LaunchModeFixPrevious,
	LaunchModeCleanup,
};
DeclareReadSetting(LaunchMode, LaunchMode);
DeclareSetting(QString, WorkingDir);
inline void cForceWorkingDir(const QString &newDir) {
	cSetWorkingDir(newDir);
	if (!gWorkingDir.isEmpty()) {
		QDir().mkpath(gWorkingDir);
		QFile::setPermissions(gWorkingDir,
			QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
	}

}
DeclareReadSetting(QString, ExeName);
DeclareReadSetting(QString, ExeDir);
DeclareSetting(QString, DialogLastPath);
DeclareSetting(QString, DialogHelperPath);
inline const QString &cDialogHelperPathFinal() {
	return cDialogHelperPath().isEmpty() ? cExeDir() : cDialogHelperPath();
}

DeclareSetting(bool, AutoUpdate);

struct TWindowPos {
	TWindowPos() = default;

	int32 moncrc = 0;
	int maximized = 0;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};
DeclareSetting(TWindowPos, WindowPos);
DeclareSetting(bool, SupportTray);
DeclareSetting(bool, SeenTrayTooltip);
DeclareSetting(bool, RestartingUpdate);
DeclareSetting(bool, Restarting);
DeclareSetting(bool, RestartingToSettings);
DeclareSetting(bool, WriteProtected);
DeclareSetting(int32, LastUpdateCheck);
DeclareSetting(bool, NoStartUpdate);
DeclareSetting(bool, StartToSettings);
DeclareReadSetting(bool, ManyInstance);

DeclareSetting(QByteArray, LocalSalt);
DeclareSetting(int, ScreenScale);
DeclareSetting(int, ConfigScale);
DeclareSetting(QString, TimeFormat);

inline void cChangeTimeFormat(const QString &newFormat) {
	if (!newFormat.isEmpty()) cSetTimeFormat(newFormat);
}

namespace Ui {
namespace Emoji {
class One;
} // namespace Emoji
} // namespace Ui

using EmojiPtr = const Ui::Emoji::One*;

using EmojiPack = QVector<EmojiPtr>;
using RecentEmojiPreloadOldOld = QVector<QPair<uint32, ushort>>;
using RecentEmojiPreloadOld = QVector<QPair<uint64, ushort>>;
using RecentEmojiPreload = QVector<QPair<QString, ushort>>;
using RecentEmojiPack = QVector<QPair<EmojiPtr, ushort>>;
using EmojiColorVariantsOld = QMap<uint32, uint64>;
using EmojiColorVariants = QMap<QString, int>;
DeclareRefSetting(RecentEmojiPack, RecentEmoji);
DeclareSetting(RecentEmojiPreload, RecentEmojiPreload);
DeclareRefSetting(EmojiColorVariants, EmojiVariants);

class DocumentData;

typedef QList<QPair<DocumentData*, int16>> RecentStickerPackOld;
typedef QVector<QPair<uint64, ushort>> RecentStickerPreload;
typedef QVector<QPair<DocumentData*, ushort>> RecentStickerPack;
DeclareSetting(RecentStickerPreload, RecentStickersPreload);
DeclareRefSetting(RecentStickerPack, RecentStickers);

typedef QList<QPair<QString, ushort>> RecentHashtagPack;
DeclareRefSetting(RecentHashtagPack, RecentWriteHashtags);
DeclareSetting(RecentHashtagPack, RecentSearchHashtags);

class UserData;
typedef QVector<UserData*> RecentInlineBots;
DeclareRefSetting(RecentInlineBots, RecentInlineBots);

DeclareSetting(bool, PasswordRecovered);

DeclareSetting(int32, PasscodeBadTries);
DeclareSetting(TimeMs, PasscodeLastTry);

inline bool passcodeCanTry() {
	if (cPasscodeBadTries() < 3) return true;
	auto dt = getms(true) - cPasscodeLastTry();
	switch (cPasscodeBadTries()) {
	case 3: return dt >= 5000;
	case 4: return dt >= 10000;
	case 5: return dt >= 15000;
	case 6: return dt >= 20000;
	case 7: return dt >= 25000;
	}
	return dt >= 30000;
}

DeclareSetting(QStringList, SendPaths);
DeclareSetting(QString, StartUrl);

DeclareSetting(float64, RetinaFactor);
DeclareSetting(int32, IntRetinaFactor);

DeclareReadSetting(DBIPlatform, Platform);
DeclareReadSetting(QString, PlatformString);
DeclareReadSetting(bool, IsElCapitan);
DeclareReadSetting(bool, IsSnowLeopard);

DeclareSetting(int, OtherOnline);

class PeerData;
typedef QMap<PeerData*, QDateTime> SavedPeers;
typedef QMultiMap<QDateTime, PeerData*> SavedPeersByTime;
DeclareRefSetting(SavedPeers, SavedPeers);
DeclareRefSetting(SavedPeersByTime, SavedPeersByTime);

typedef QMap<uint64, DBIPeerReportSpamStatus> ReportSpamStatuses;
DeclareRefSetting(ReportSpamStatuses, ReportSpamStatuses);

DeclareSetting(bool, AutoPlayGif);

constexpr auto kInterfaceScaleAuto = 0;
constexpr auto kInterfaceScaleMin = 100;
constexpr auto kInterfaceScaleDefault = 100;
constexpr auto kInterfaceScaleMax = 300;

inline int cEvalScale(int scale) {
	return (scale == kInterfaceScaleAuto) ? cScreenScale() : scale;
}

inline int cScale() {
	return cEvalScale(cConfigScale());
}

template <typename T>
inline T ConvertScale(T value, int scale) {
	return (value < 0.)
		? (-ConvertScale(-value, scale))
		: T(std::round((float64(value) * scale / 100.) - 0.01));
}

template <typename T>
inline T ConvertScale(T value) {
	return ConvertScale(value, cScale());
}

inline void SetScaleChecked(int scale) {
	const auto checked = (scale == kInterfaceScaleAuto)
		? kInterfaceScaleAuto
		: snap(scale, kInterfaceScaleMin, kInterfaceScaleMax / cIntRetinaFactor());
	cSetConfigScale(checked);
}
