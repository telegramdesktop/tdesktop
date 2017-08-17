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
#pragma once

#include "base/type_traits.h"
#include "base/observer.h"

class LayerWidget;
class BoxContent;

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace InlineBots

namespace App {
namespace internal {

void CallDelayed(int duration, base::lambda_once<void()> &&lambda);

} // namespace internal

template <int N, typename Lambda>
inline void CallDelayed(int duration, base::lambda_internal::guard<N, Lambda> &&guarded) {
	return internal::CallDelayed(duration, [guarded = std::move(guarded)] { guarded(); });
}

template <typename Pointer, typename ...PointersAndLambda>
inline void CallDelayed(int duration, Pointer &&qobject, PointersAndLambda&&... qobjectsAndLambda) {
	auto guarded = base::lambda_guarded(std::forward<Pointer>(qobject), std::forward<PointersAndLambda>(qobjectsAndLambda)...);
	return CallDelayed(duration, std::move(guarded));
}

template <typename ...PointersAndLambda>
inline base::lambda<void()> LambdaDelayed(int duration, PointersAndLambda&&... qobjectsAndLambda) {
	auto guarded = base::lambda_guarded(std::forward<PointersAndLambda>(qobjectsAndLambda)...);
	return [guarded = std::move(guarded), duration] {
		internal::CallDelayed(duration, [guarded] { guarded(); });
	};
}

template <typename ...PointersAndLambda>
inline base::lambda_once<void()> LambdaDelayedOnce(int duration, PointersAndLambda&&... qobjectsAndLambda) {
	auto guarded = base::lambda_guarded(std::forward<PointersAndLambda>(qobjectsAndLambda)...);
	return [guarded = std::move(guarded), duration]() mutable {
		internal::CallDelayed(duration, [guarded = std::move(guarded)] { guarded(); });
	};
}

void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo = 0);
bool insertBotCommand(const QString &cmd);
void activateBotCommand(const HistoryItem *msg, int row, int col);
void searchByHashtag(const QString &tag, PeerData *inPeer);
void openPeerByName(const QString &username, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
void joinGroupByHash(const QString &hash);
void stickersBox(const QString &name);
void removeDialog(History *history);
void showSettings();

void activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button);

void logOutDelayed();

} // namespace App

namespace Ui {
namespace internal {

void showBox(object_ptr<BoxContent> content, ShowLayerOptions options);

} // namespace internal

void showMediaPreview(DocumentData *document);
void showMediaPreview(PhotoData *photo);
void hideMediaPreview();

template <typename BoxType>
QPointer<BoxType> show(object_ptr<BoxType> content, ShowLayerOptions options = CloseOtherLayers) {
	auto result = QPointer<BoxType>(content.data());
	internal::showBox(std::move(content), options);
	return result;
}

void hideLayer(bool fast = false);
void hideSettingsAndLayer(bool fast = false);
bool isLayerShown();

void repaintHistoryItem(not_null<const HistoryItem*> item);
void autoplayMediaInlineAsync(const FullMsgId &msgId);

void showPeerProfile(const PeerId &peer);
inline void showPeerProfile(const PeerData *peer) {
	showPeerProfile(peer->id);
}
inline void showPeerProfile(const History *history) {
	showPeerProfile(history->peer->id);
}

void showPeerOverview(const PeerId &peer, MediaOverviewType type);
inline void showPeerOverview(const PeerData *peer, MediaOverviewType type) {
	showPeerOverview(peer->id, type);
}
inline void showPeerOverview(const History *history, MediaOverviewType type) {
	showPeerOverview(history->peer->id, type);
}

enum class ShowWay {
	ClearStack,
	Forward,
	Backward,
};
void showPeerHistory(const PeerId &peer, MsgId msgId, ShowWay way = ShowWay::ClearStack);
inline void showPeerHistory(const PeerData *peer, MsgId msgId, ShowWay way = ShowWay::ClearStack) {
	showPeerHistory(peer->id, msgId, way);
}
inline void showPeerHistory(const History *history, MsgId msgId, ShowWay way = ShowWay::ClearStack) {
	showPeerHistory(history->peer->id, msgId, way);
}
inline void showPeerHistoryAtItem(const HistoryItem *item, ShowWay way = ShowWay::ClearStack) {
	showPeerHistory(item->history()->peer->id, item->id, way);
}
void showPeerHistoryAsync(const PeerId &peer, MsgId msgId, ShowWay way = ShowWay::ClearStack);
inline void showChatsList() {
	showPeerHistory(PeerId(0), 0, ShowWay::ClearStack);
}
inline void showChatsListAsync() {
	showPeerHistoryAsync(PeerId(0), 0, ShowWay::ClearStack);
}
PeerData *getPeerForMouseAction();

bool skipPaintEvent(QWidget *widget, QPaintEvent *event);

} // namespace Ui

enum ClipStopperType {
	ClipStopperMediaview,
	ClipStopperSavedGifsPanel,
};

namespace Notify {

void userIsBotChanged(UserData *user);
void userIsContactChanged(UserData *user, bool fromThisApp = false);
void botCommandsChanged(UserData *user);

void inlineBotRequesting(bool requesting);
void replyMarkupUpdated(const HistoryItem *item);
void inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
bool switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot = nullptr, MsgId samePeerReplyTo = 0);

void migrateUpdated(PeerData *peer);

void historyItemLayoutChanged(const HistoryItem *item);
void historyMuteUpdated(History *history);

// handle pending resize() / paint() on history items
void handlePendingHistoryUpdate();
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

namespace Sandbox {

bool CheckBetaVersionDir();
void WorkingDirReady();

void MainThreadTaskAdded();

void start();
bool started();
void finish();

uint64 UserTag();

DeclareVar(QByteArray, LastCrashDump);
DeclareVar(ProxyData, PreLaunchProxy);

} // namespace Sandbox

namespace Adaptive {

enum class WindowLayout {
	OneColumn,
	SmallColumn,
	Normal,
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

namespace Stickers {

constexpr auto DefaultSetId = 0; // for backward compatibility
constexpr auto CustomSetId = 0xFFFFFFFFFFFFFFFFULL;
constexpr auto RecentSetId = 0xFFFFFFFFFFFFFFFEULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto NoneSetId = 0xFFFFFFFFFFFFFFFDULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto CloudRecentSetId = 0xFFFFFFFFFFFFFFFCULL; // for cloud-stored recent stickers
constexpr auto FeaturedSetId = 0xFFFFFFFFFFFFFFFBULL; // for emoji/stickers panel, should not appear in Sets
constexpr auto FavedSetId = 0xFFFFFFFFFFFFFFFAULL; // for cloud-stored faved stickers
constexpr auto MegagroupSetId = 0xFFFFFFFFFFFFFFEFULL; // for setting up megagroup sticker set
struct Set {
	Set(uint64 id, uint64 access, const QString &title, const QString &shortName, int32 count, int32 hash, MTPDstickerSet::Flags flags)
		: id(id)
		, access(access)
		, title(title)
		, shortName(shortName)
		, count(count)
		, hash(hash)
		, flags(flags) {
	}
	uint64 id, access;
	QString title, shortName;
	int32 count, hash;
	MTPDstickerSet::Flags flags;
	StickerPack stickers;
	StickersByEmojiMap emoji;
};
using Sets = QMap<uint64, Set>;
using Order = QList<uint64>;

inline MTPInputStickerSet inputSetId(const Set &set) {
	if (set.id && set.access) {
		return MTP_inputStickerSetID(MTP_long(set.id), MTP_long(set.access));
	}
	return MTP_inputStickerSetShortName(MTP_string(set.shortName));
}

} // namespace Stickers

namespace Global {

bool started();
void start();
void finish();

DeclareRefVar(SingleQueuedInvokation, HandleHistoryUpdate);
DeclareRefVar(SingleQueuedInvokation, HandleUnreadCounterUpdate);
DeclareRefVar(SingleQueuedInvokation, HandleDelayedPeerUpdates);
DeclareRefVar(SingleQueuedInvokation, HandleObservables);

DeclareVar(Adaptive::WindowLayout, AdaptiveWindowLayout);
DeclareVar(Adaptive::ChatLayout, AdaptiveChatLayout);
DeclareVar(bool, AdaptiveForWide);
DeclareRefVar(base::Observable<void>, AdaptiveChanged);

DeclareVar(bool, DialogsModeEnabled);
DeclareVar(Dialogs::Mode, DialogsMode);
DeclareVar(bool, ModerateModeEnabled);

DeclareVar(bool, ScreenIsLocked);

DeclareVar(int32, DebugLoggingFlags);

constexpr float64 kDefaultVolume = 0.9;

DeclareVar(float64, RememberedSongVolume);
DeclareVar(float64, SongVolume);
DeclareRefVar(base::Observable<void>, SongVolumeChanged);
DeclareVar(float64, VideoVolume);
DeclareRefVar(base::Observable<void>, VideoVolumeChanged);

// config
DeclareVar(int32, ChatSizeMax);
DeclareVar(int32, MegagroupSizeMax);
DeclareVar(int32, ForwardedCountMax);
DeclareVar(int32, OnlineUpdatePeriod);
DeclareVar(int32, OfflineBlurTimeout);
DeclareVar(int32, OfflineIdleTimeout);
DeclareVar(int32, OnlineFocusTimeout); // not from config
DeclareVar(int32, OnlineCloudTimeout);
DeclareVar(int32, NotifyCloudDelay);
DeclareVar(int32, NotifyDefaultDelay);
DeclareVar(int32, ChatBigSize);
DeclareVar(int32, PushChatPeriod);
DeclareVar(int32, PushChatLimit);
DeclareVar(int32, SavedGifsLimit);
DeclareVar(int32, EditTimeLimit);
DeclareVar(int32, StickersRecentLimit);
DeclareVar(int32, StickersFavedLimit);
DeclareVar(int32, PinnedDialogsCountMax);
DeclareVar(QString, InternalLinksDomain);
DeclareVar(int32, CallReceiveTimeoutMs);
DeclareVar(int32, CallRingTimeoutMs);
DeclareVar(int32, CallConnectTimeoutMs);
DeclareVar(int32, CallPacketTimeoutMs);
DeclareVar(bool, PhoneCallsEnabled);
DeclareRefVar(base::Observable<void>, PhoneCallsEnabledChanged);

typedef QMap<PeerId, MsgId> HiddenPinnedMessagesMap;
DeclareVar(HiddenPinnedMessagesMap, HiddenPinnedMessages);

typedef OrderedSet<HistoryItem*> PendingItemsMap;
DeclareRefVar(PendingItemsMap, PendingRepaintItems);

DeclareVar(Stickers::Sets, StickerSets);
DeclareVar(Stickers::Order, StickerSetsOrder);
DeclareVar(TimeMs, LastStickersUpdate);
DeclareVar(TimeMs, LastRecentStickersUpdate);
DeclareVar(TimeMs, LastFavedStickersUpdate);
DeclareVar(Stickers::Order, FeaturedStickerSetsOrder);
DeclareVar(int, FeaturedStickerSetsUnreadCount);
DeclareRefVar(base::Observable<void>, FeaturedStickerSetsUnreadCountChanged);
DeclareVar(TimeMs, LastFeaturedStickersUpdate);
DeclareVar(Stickers::Order, ArchivedStickerSetsOrder);

typedef QMap<uint64, QPixmap> CircleMasksMap;
DeclareRefVar(CircleMasksMap, CircleMasks);

DeclareRefVar(base::Observable<void>, SelfChanged);

DeclareVar(bool, AskDownloadPath);
DeclareVar(QString, DownloadPath);
DeclareVar(QByteArray, DownloadPathBookmark);
DeclareRefVar(base::Observable<void>, DownloadPathChanged);

DeclareVar(bool, SoundNotify);
DeclareVar(bool, DesktopNotify);
DeclareVar(bool, RestoreSoundNotifyFromTray);
DeclareVar(bool, IncludeMuted);
DeclareVar(DBINotifyView, NotifyView);
DeclareVar(bool, NativeNotifications);
DeclareVar(int, NotificationsCount);
DeclareVar(Notify::ScreenCorner, NotificationsCorner);
DeclareVar(bool, NotificationsDemoIsShown);

DeclareVar(DBIConnectionType, ConnectionType);
DeclareVar(DBIConnectionType, LastProxyType);
DeclareVar(bool, TryIPv6);
DeclareVar(ProxyData, ConnectionProxy);
DeclareRefVar(base::Observable<void>, ConnectionTypeChanged);

DeclareVar(int, AutoLock);
DeclareVar(bool, LocalPasscode);
DeclareRefVar(base::Observable<void>, LocalPasscodeChanged);

DeclareRefVar(base::Variable<DBIWorkMode>, WorkMode);

DeclareRefVar(base::Observable<HistoryItem*>, ItemRemoved);
DeclareRefVar(base::Observable<void>, UnreadCounterUpdate);
DeclareRefVar(base::Observable<void>, PeerChooseCancel);

} // namespace Global

namespace Adaptive {

inline base::Observable<void> &Changed() {
	return Global::RefAdaptiveChanged();
}

inline bool OneColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::OneColumn;
}

inline bool SmallColumn() {
	return Global::AdaptiveWindowLayout() == WindowLayout::SmallColumn;
}

inline bool Normal() {
	return Global::AdaptiveWindowLayout() == WindowLayout::Normal;
}

inline bool ChatNormal() {
	return !Global::AdaptiveForWide() || (Global::AdaptiveChatLayout() == ChatLayout::Normal);
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
