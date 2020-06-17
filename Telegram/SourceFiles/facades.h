/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/type_traits.h"
#include "base/observer.h"
#include "base/call_delayed.h"
#include "mtproto/mtproto_proxy_data.h"

class History;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace InlineBots

namespace App {

template <typename Guard, typename Lambda>
[[nodiscard]] inline auto LambdaDelayed(int duration, Guard &&object, Lambda &&lambda) {
	auto guarded = crl::guard(
		std::forward<Guard>(object),
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration] {
		auto copy = saved;
		base::call_delayed(duration, std::move(copy));
	};
}

void sendBotCommand(
	not_null<PeerData*> peer,
	UserData *bot,
	const QString &cmd,
	MsgId replyTo = 0);
bool insertBotCommand(const QString &cmd);
void activateBotCommand(
	not_null<const HistoryItem*> msg,
	int row,
	int column);
void searchByHashtag(const QString &tag, PeerData *inPeer);
void showSettings();

} // namespace App

namespace Ui {

// Legacy global methods.

void showPeerProfile(const PeerId &peer);
void showPeerProfile(const PeerData *peer);
void showPeerProfile(not_null<const History*> history);

void showPeerHistory(const PeerId &peer, MsgId msgId);
void showPeerHistoryAtItem(not_null<const HistoryItem*> item);

void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId);
void showPeerHistory(not_null<const History*> history, MsgId msgId);
inline void showChatsList() {
	showPeerHistory(PeerId(0), 0);
}
PeerData *getPeerForMouseAction();

bool skipPaintEvent(QWidget *widget, QPaintEvent *event);

} // namespace Ui

enum ClipStopperType {
	ClipStopperMediaview,
	ClipStopperSavedGifsPanel,
};

namespace Notify {

void replyMarkupUpdated(not_null<const HistoryItem*> item);
void inlineKeyboardMoved(
	not_null<const HistoryItem*> item,
	int oldKeyboardTop,
	int newKeyboardTop);
bool switchInlineBotButtonReceived(
	not_null<Main::Session*> session,
	const QString &query,
	UserData *samePeerBot = nullptr,
	MsgId samePeerReplyTo = 0);

void unreadCounterUpdated();

enum class ScreenCorner {
	TopLeft     = 0,
	TopRight    = 1,
	BottomRight = 2,
	BottomLeft  = 3,
};

inline bool IsLeftCorner(ScreenCorner corner) {
	return (corner == ScreenCorner::TopLeft) || (corner == ScreenCorner::BottomLeft);
}

inline bool IsTopCorner(ScreenCorner corner) {
	return (corner == ScreenCorner::TopLeft) || (corner == ScreenCorner::TopRight);
}

} // namespace Notify

#define DeclareReadOnlyVar(Type, Name) const Type &Name();
#define DeclareRefVar(Type, Name) DeclareReadOnlyVar(Type, Name) \
	Type &Ref##Name();
#define DeclareVar(Type, Name) DeclareRefVar(Type, Name) \
	void Set##Name(const Type &Name);

namespace Adaptive {

enum class WindowLayout {
	OneColumn,
	Normal,
	ThreeColumn,
};

enum class ChatLayout {
	Normal,
	Wide,
};

} // namespace Adaptive

namespace DebugLogging {
enum Flags {
	FileLoaderFlag = 0x00000001,
};
} // namespace DebugLogging

namespace Global {

bool started();
void start();
void finish();

DeclareRefVar(SingleQueuedInvokation, HandleUnreadCounterUpdate);

DeclareVar(Adaptive::WindowLayout, AdaptiveWindowLayout);
DeclareVar(Adaptive::ChatLayout, AdaptiveChatLayout);
DeclareVar(bool, AdaptiveForWide);
DeclareRefVar(base::Observable<void>, AdaptiveChanged);

DeclareVar(bool, DialogsFiltersEnabled);
DeclareVar(bool, ModerateModeEnabled);

DeclareVar(bool, ScreenIsLocked);

DeclareVar(int32, DebugLoggingFlags);

constexpr float64 kDefaultVolume = 0.9;

DeclareVar(float64, RememberedSongVolume);
DeclareVar(float64, SongVolume);
DeclareRefVar(base::Observable<void>, SongVolumeChanged);
DeclareVar(float64, VideoVolume);
DeclareRefVar(base::Observable<void>, VideoVolumeChanged);

typedef QMap<PeerId, MsgId> HiddenPinnedMessagesMap;
DeclareVar(HiddenPinnedMessagesMap, HiddenPinnedMessages);

DeclareVar(bool, AskDownloadPath);
DeclareVar(QString, DownloadPath);
DeclareVar(QByteArray, DownloadPathBookmark);
DeclareRefVar(base::Observable<void>, DownloadPathChanged);

DeclareVar(bool, VoiceMsgPlaybackDoubled);
DeclareVar(bool, SoundNotify);
DeclareVar(bool, DesktopNotify);
DeclareVar(bool, FlashBounceNotify);
DeclareVar(bool, RestoreSoundNotifyFromTray);
DeclareVar(bool, RestoreFlashBounceNotifyFromTray);
DeclareVar(DBINotifyView, NotifyView);
DeclareVar(bool, NativeNotifications);
DeclareVar(int, NotificationsCount);
DeclareVar(Notify::ScreenCorner, NotificationsCorner);
DeclareVar(bool, NotificationsDemoIsShown);

DeclareVar(bool, TryIPv6);
DeclareVar(std::vector<MTP::ProxyData>, ProxiesList);
DeclareVar(MTP::ProxyData, SelectedProxy);
DeclareVar(MTP::ProxyData::Settings, ProxySettings);
DeclareVar(bool, UseProxyForCalls);
DeclareRefVar(base::Observable<void>, ConnectionTypeChanged);

DeclareVar(int, AutoLock);
DeclareVar(bool, LocalPasscode);
DeclareRefVar(base::Observable<void>, LocalPasscodeChanged);

DeclareRefVar(base::Variable<DBIWorkMode>, WorkMode);

DeclareRefVar(base::Observable<void>, UnreadCounterUpdate);
DeclareRefVar(base::Observable<void>, PeerChooseCancel);

DeclareVar(QString, CallOutputDeviceID);
DeclareVar(QString, CallInputDeviceID);
DeclareVar(int, CallOutputVolume);
DeclareVar(int, CallInputVolume);
DeclareVar(bool, CallAudioDuckingEnabled);

} // namespace Global

namespace Adaptive {

inline base::Observable<void> &Changed() {
	return Global::RefAdaptiveChanged();
}

inline bool OneColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::OneColumn;
}

inline bool Normal() {
	return Global::AdaptiveWindowLayout() == WindowLayout::Normal;
}

inline bool ThreeColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::ThreeColumn;
}

inline bool ChatNormal() {
	return !Global::AdaptiveForWide()
		|| (Global::AdaptiveChatLayout() == ChatLayout::Normal);
}

inline bool ChatWide() {
	return !ChatNormal();
}

} // namespace Adaptive

namespace DebugLogging {

inline bool FileLoader() {
	return (Global::DebugLoggingFlags() & FileLoaderFlag) != 0;
}

} // namespace DebugLogging
