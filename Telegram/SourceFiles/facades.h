/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/type_traits.h"
#include "base/observer.h"

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

template <typename Lambda>
inline void CallDelayed(
		int duration,
		base::lambda_internal::guard_with_QObject<Lambda> &&guarded) {
	return internal::CallDelayed(
		duration,
		std::move(guarded));
}

template <typename Lambda>
inline void CallDelayed(
		int duration,
		base::lambda_internal::guard_with_weak<Lambda> &&guarded) {
	return internal::CallDelayed(
		duration,
		std::move(guarded));
}

template <typename Lambda>
inline void CallDelayed(
		int duration,
		const QObject *object,
		Lambda &&lambda) {
	return internal::CallDelayed(
		duration,
		base::lambda_guarded(object, std::forward<Lambda>(lambda)));
}

template <typename Lambda>
inline void CallDelayed(
		int duration,
		const base::has_weak_ptr *object,
		Lambda &&lambda) {
	return internal::CallDelayed(
		duration,
		base::lambda_guarded(object, std::forward<Lambda>(lambda)));
}

template <typename Lambda>
inline auto LambdaDelayed(
		int duration,
		const QObject *object,
		Lambda &&lambda) {
	auto guarded = base::lambda_guarded(
		object,
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration] {
		auto copy = saved;
		internal::CallDelayed(duration, std::move(copy));
	};
}

template <typename Lambda>
inline auto LambdaDelayed(
		int duration,
		const base::has_weak_ptr *object,
		Lambda &&lambda) {
	auto guarded = base::lambda_guarded(
		object,
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration] {
		auto copy = saved;
		internal::CallDelayed(duration, std::move(copy));
	};
}

template <typename Lambda>
inline auto LambdaDelayedOnce(
		int duration,
		const QObject *object,
		Lambda &&lambda) {
	auto guarded = base::lambda_guarded(
		object,
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration]() mutable {
		internal::CallDelayed(duration, std::move(saved));
	};
}

template <typename Lambda>
inline auto LambdaDelayedOnce(
		int duration,
		const base::has_weak_ptr *object,
		Lambda &&lambda) {
	auto guarded = base::lambda_guarded(
		object,
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration]() mutable {
		internal::CallDelayed(duration, std::move(saved));
	};
}

void sendBotCommand(
	PeerData *peer,
	UserData *bot,
	const QString &cmd,
	MsgId replyTo = 0);
bool insertBotCommand(const QString &cmd);
void activateBotCommand(
	not_null<const HistoryItem*> msg,
	int row,
	int column);
void searchByHashtag(const QString &tag, PeerData *inPeer);
void openPeerByName(
	const QString &username,
	MsgId msgId = ShowAtUnreadMsgId,
	const QString &startToken = QString());
void joinGroupByHash(const QString &hash);
void removeDialog(History *history);
void showSettings();

void activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button);

void logOutDelayed();

} // namespace App


enum class LayerOption {
	CloseOther = (1 << 0),
	KeepOther = (1 << 1),
	ShowAfterOther = (1 << 2),
};
using LayerOptions = base::flags<LayerOption>;
inline constexpr auto is_flag_type(LayerOption) { return true; };

namespace Ui {
namespace internal {

void showBox(
	object_ptr<BoxContent> content,
	LayerOptions options,
	anim::type animated);

} // namespace internal

void showMediaPreview(DocumentData *document);
void showMediaPreview(PhotoData *photo);
void hideMediaPreview();

template <typename BoxType>
QPointer<BoxType> show(
		object_ptr<BoxType> content,
		LayerOptions options = LayerOption::CloseOther,
		anim::type animated = anim::type::normal) {
	auto result = QPointer<BoxType>(content.data());
	internal::showBox(std::move(content), options, animated);
	return result;
}

void hideLayer(anim::type animated = anim::type::normal);
void hideSettingsAndLayer(anim::type animated = anim::type::normal);
bool isLayerShown();

void autoplayMediaInlineAsync(const FullMsgId &msgId);

void showPeerProfile(const PeerId &peer);
inline void showPeerProfile(const PeerData *peer) {
	showPeerProfile(peer->id);
}
inline void showPeerProfile(const History *history) {
	showPeerProfile(history->peer->id);
}

void showPeerHistory(const PeerId &peer, MsgId msgId);

inline void showPeerHistory(const PeerData *peer, MsgId msgId) {
	showPeerHistory(peer->id, msgId);
}
inline void showPeerHistory(
		const History *history,
		MsgId msgId) {
	showPeerHistory(history->peer->id, msgId);
}
inline void showPeerHistoryAtItem(const HistoryItem *item) {
	showPeerHistory(item->history()->peer->id, item->id);
}
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

void userIsBotChanged(UserData *user);
void userIsContactChanged(UserData *user, bool fromThisApp = false);
void botCommandsChanged(UserData *user);

void inlineBotRequesting(bool requesting);
void replyMarkupUpdated(const HistoryItem *item);
void inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
bool switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot = nullptr, MsgId samePeerReplyTo = 0);

void migrateUpdated(PeerData *peer);

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
DeclareVar(int32, ChannelsReadMediaPeriod);
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
