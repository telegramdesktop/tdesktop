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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "localimageloader.h"
#include "history/history_common.h"

namespace Dialogs {
class Row;
} // namespace Dialogs

namespace Ui {
class PeerAvatarButton;
} // namespace Ui

namespace Window {
class TopBarWidget;
} // namespace Window

class MainWindow;
class ApiWrap;
class ConfirmBox;
class DialogsWidget;
class HistoryWidget;
class ProfileWidget;
class OverviewWidget;
class PlayerWidget;
class HistoryHider;
class Dropdown;

enum StackItemType {
	HistoryStackItem,
	ProfileStackItem,
	OverviewStackItem,
};

class StackItem {
public:
	StackItem(PeerData *peer) : peer(peer) {
	}
	virtual StackItemType type() const = 0;
	virtual ~StackItem() {
	}
	PeerData *peer;
};

class StackItemHistory : public StackItem {
public:
	StackItemHistory(PeerData *peer, MsgId msgId, QList<MsgId> replyReturns) : StackItem(peer),
msgId(msgId), replyReturns(replyReturns) {
	}
	StackItemType type() const {
		return HistoryStackItem;
	}
	MsgId msgId;
	QList<MsgId> replyReturns;
};

class StackItemProfile : public StackItem {
public:
	StackItemProfile(PeerData *peer, int32 lastScrollTop) : StackItem(peer), lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return ProfileStackItem;
	}
	int32 lastScrollTop;
};

class StackItemOverview : public StackItem {
public:
	StackItemOverview(PeerData *peer, MediaOverviewType mediaType, int32 lastWidth, int32 lastScrollTop) : StackItem(peer), mediaType(mediaType), lastWidth(lastWidth), lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return OverviewStackItem;
	}
	MediaOverviewType mediaType;
	int32 lastWidth, lastScrollTop;
};

class StackItems : public QVector<StackItem*> {
public:
	bool contains(PeerData *peer) const {
		for (int32 i = 0, l = size(); i < l; ++i) {
			if (at(i)->peer == peer) {
				return true;
			}
		}
		return false;
	}
	void clear() {
		for (int32 i = 0, l = size(); i < l; ++i) {
			delete at(i);
		}
		QVector<StackItem*>::clear();
	}
	~StackItems() {
		clear();
	}
};

inline int chatsListWidth(int windowWidth) {
	return snap<int>((windowWidth * 5) / 14, st::dlgMinWidth, st::dlgMaxWidth);
}

enum SilentNotifiesStatus {
	SilentNotifiesDontChange,
	SilentNotifiesSetSilent,
	SilentNotifiesSetNotify,
};
enum NotifySettingStatus {
	NotifySettingDontChange,
	NotifySettingSetMuted,
	NotifySettingSetNotify,
};

namespace InlineBots {
namespace Layout {

class ItemBase;

} // namespace Layout
} // namespace InlineBots

class MainWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	MainWidget(MainWindow *window);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void updateAdaptiveLayout();
	bool needBackButton();

	void paintTopBar(Painter &p, float64 over, int32 decreaseWidth);
	Window::TopBarWidget *topBar();

	PlayerWidget *player();
	int contentScrollAddToY() const;

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	void step_show(float64 ms, bool timer);
	void animStop_show();

	void start(const MTPUser &user);

	void openLocalUrl(const QString &str);
	void openPeerByName(const QString &name, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const MTPInputStickerSet &set);

	void startFull(const MTPVector<MTPUser> &users);
	bool started();
	void applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history = 0);
	void gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	bool failNotifySetting(MTPInputNotifyPeer peer, const RPCError &error);

	void updateNotifySetting(PeerData *peer, NotifySettingStatus notify, SilentNotifiesStatus silent = SilentNotifiesDontChange);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialog(History *history);
	void removeDialog(History *history);
	void dlgUpdated();
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(History *row, MsgId msgId);

	void windowShown();

	void sentUpdatesReceived(uint64 randomId, const MTPUpdates &updates);
	void sentUpdatesReceived(const MTPUpdates &updates) {
		return sentUpdatesReceived(0, updates);
	}
	bool deleteChannelFailed(const RPCError &error);
	void inviteToChannelDone(ChannelData *channel, const MTPUpdates &updates);
	void historyToDown(History *hist);
	void dialogsToUp();
	void newUnreadMsg(History *history, HistoryItem *item);
	void historyWasRead();
	void historyCleared(History *history);

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	PeerData *historyPeer();
	PeerData *peer();

	PeerData *activePeer();
	MsgId activeMsgId();

	PeerData *profilePeer();
	PeerData *overviewPeer();
	bool mediaTypeSwitch();
	void showPeerProfile(PeerData *peer, bool back = false, int32 lastScrollTop = -1);
	void showMediaOverview(PeerData *peer, MediaOverviewType type, bool back = false, int32 lastScrollTop = -1);
	void showBackFromStack();
	void orderWidgets();
	QRect historyRect() const;

	void onSendFileConfirm(const FileLoadResultPtr &file, bool ctrlShiftEnter);
	void onSendFileCancel(const FileLoadResultPtr &file);
	void onShareContactConfirm(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool ctrlShiftEnter);
	void onShareContactCancel();

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	bool isActive() const;
	bool historyIsActive() const;
	bool lastWasOnline() const;
	uint64 lastSetOnline() const;

	int32 dlgsWidth() const;

	void forwardLayer(int32 forwardSelected = 0); // -1 - send paths
	void deleteLayer(int32 selectedCount = -1); // -1 - context item, else selected, -2 - cancel upload
	void shareContactLayer(UserData *contact);
	void shareUrlLayer(const QString &url, const QString &text);
	void inlineSwitchLayer(const QString &botAndQuery);
	void hiderLayer(HistoryHider *h);
	void noHider(HistoryHider *destroyed);
	bool onForward(const PeerId &peer, ForwardWhatMessages what);
	bool onShareUrl(const PeerId &peer, const QString &url, const QString &text);
	bool onInlineSwitchChosen(const PeerId &peer, const QString &botAndQuery);
	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer(bool withConfirm = false);
	bool selectingPeerForInlineSwitch();
	void offerPeer(PeerId peer);
	void dialogsActivate();

	DragState getDragState(const QMimeData *mime);

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates);
	void deleteMessages(PeerData *peer, const QVector<MTPint> &ids);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteConversation(PeerData *peer, bool deleteHistory = true);
	void clearHistory(PeerData *peer);
	void deleteAllFromUser(ChannelData *channel, UserData *from);

	void addParticipants(PeerData *chatOrChannel, const QVector<UserData*> &users);
	bool addParticipantFail(UserData *user, const RPCError &e);
	bool addParticipantsFail(ChannelData *channel, const RPCError &e); // for multi invite in channels

	void kickParticipant(ChatData *chat, UserData *user);
	bool kickParticipantFail(ChatData *chat, const RPCError &e);

	void checkPeerHistory(PeerData *peer);
	void checkedHistory(PeerData *peer, const MTPmessages_Messages &result);

	bool sendMessageFail(const RPCError &error);

	void forwardSelectedItems();
	void deleteSelectedItems();
	void clearSelectedItems();

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();

	void sendMessage(History *hist, const QString &text, MsgId replyTo, bool broadcast, bool silent, WebPageId webPageId = 0);
	void saveRecentHashtags(const QString &text);

    void readServerHistory(History *history, bool force = true);

	uint64 animActiveTimeStart(const HistoryItem *msg) const;
	void stopAnimActive();

	void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo);
	bool insertBotCommand(const QString &cmd, bool specialGif);

	void searchMessages(const QString &query, PeerData *inPeer);
	bool preloadOverview(PeerData *peer, MediaOverviewType type);
	void preloadOverviews(PeerData *peer);
	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);
	void itemEdited(HistoryItem *item);

	void loadMediaBack(PeerData *peer, MediaOverviewType type, bool many = false);
	void peerUsernameChanged(PeerData *peer);

	void checkLastUpdate(bool afterSleep);
	void showAddContact();
	void showNewGroup();

	void serviceNotification(const QString &msg, const MTPMessageMedia &media);
	void serviceHistoryDone(const MTPmessages_Messages &msgs);
	bool serviceHistoryFail(const RPCError &error);

	bool isIdle() const;

	void clearCachedBackground();
	QPixmap cachedBackground(const QRect &forRect, int &x, int &y);
	void backgroundParams(const QRect &forRect, QRect &to, QRect &from) const;
	void updateScrollColors();

	void setChatBackground(const App::WallPaper &wp);
	bool chatBackgroundLoading();
	void checkChatBackground();
	ImagePtr newBackgroundThumb();

	ApiWrap *api();
	void messageDataReceived(ChannelData *channel, MsgId msgId);
	void updateBotKeyboard(History *h);

	void pushReplyReturn(HistoryItem *item);

	bool hasForwardingItems();
	void fillForwardingInfo(Text *&from, Text *&text, bool &serviceColor, ImagePtr &preview);
	void updateForwardingTexts();
	void cancelForwarding();
	void finishForwarding(History *hist, bool broadcast, bool silent); // send them

	void mediaMarkRead(DocumentData *data);
	void mediaMarkRead(const HistoryItemsMap &items);

	void webPageUpdated(WebPageData *page);
	void updateMutedIn(int32 seconds);

	void updateStickers();

	void choosePeer(PeerId peerId, MsgId showAtMsgId); // does offerPeer or showPeerHistory
	void clearBotStartToken(PeerData *peer);

	void contactsReceived();

	void ptsWaiterStartTimerFor(ChannelData *channel, int32 ms); // ms <= 0 - stop timer
	void feedUpdates(const MTPUpdates &updates, uint64 randomId = 0);
	void feedUpdate(const MTPUpdate &update);
	void updateAfterDrag();

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	HistoryItem *atTopImportantMsg(int32 &bottomUnderScrollTop) const;

	void gotRangeDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff);
	void onSelfParticipantUpdated(ChannelData *channel);

	bool contentOverlapped(const QRect &globalRect);

	QPixmap grabTopBar();
	QPixmap grabInner();

	void rpcClear() override;

	bool isItemVisible(HistoryItem *item);

	void closePlayer();

	void app_sendBotCallback(const HistoryMessageReplyMarkup::Button *button, const HistoryItem *msg, int row, int col);

	void ui_repaintHistoryItem(const HistoryItem *item);
	void ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemBeingChosen();
	void ui_showPeerHistory(quint64 peer, qint32 msgId, bool back);
	PeerData *ui_getPeerForMouseAction();

	void notify_botCommandsChanged(UserData *bot);
	void notify_inlineBotRequesting(bool requesting);
	void notify_replyMarkupUpdated(const HistoryItem *item);
	void notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
	bool notify_switchInlineBotButtonReceived(const QString &query);
	void notify_userIsBotChanged(UserData *bot);
	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_migrateUpdated(PeerData *peer);
	void notify_clipStopperHidden(ClipStopperType type);
	void notify_historyItemLayoutChanged(const HistoryItem *item);
	void notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout);
	void notify_historyMuteUpdated(History *history);
	void notify_handlePendingHistoryUpdate();

	void cmd_search();
	void cmd_next_chat();
	void cmd_previous_chat();

	~MainWidget();

signals:

	void peerUpdated(PeerData *peer);
	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void peerPhotoChanged(PeerData *peer);
	void dialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);
	void dialogsUpdated();
	void stickersUpdated();
	void savedGifsUpdated();

public slots:

	void webPagesUpdate();

	void audioPlayProgress(const AudioMsgId &audioId);
	void documentLoadProgress(FileLoader *loader);
	void documentLoadFailed(FileLoader *loader, bool started);
	void documentLoadRetry();
	void documentPlayProgress(const SongMsgId &songId);
	void inlineResultLoadProgress(FileLoader *loader);
	void inlineResultLoadFailed(FileLoader *loader, bool started);

	void dialogsCancelled();

	void onParentResize(const QSize &newSize);
	void getDifference();
	void onGetDifferenceTimeByPts();
	void onGetDifferenceTimeAfterFail();
	void mtpPing();

	void updateOnline(bool gotOtherOffline = false);
	void checkIdleFinish();
	void updateOnlineDisplay();

	void onTopBarClick();
	void onHistoryShown(History *history, MsgId atMsgId);

	void searchInPeer(PeerData *peer);

	void onUpdateNotifySettings();

	void onPhotosSelect();
	void onVideosSelect();
	void onSongsSelect();
	void onDocumentsSelect();
	void onAudiosSelect();
	void onLinksSelect();

	void onForwardCancel(QObject *obj = 0);

	void onCacheBackground();

	void onInviteImport();

	void onUpdateMuted();

	void onStickersInstalled(uint64 setId);
	void onFullPeerUpdated(PeerData *peer);

	void onViewsIncrement();
	void onActiveChannelUpdateFull();

	void onDownloadPathSettings();

	void onSharePhoneWithBot(PeerData *recipient);

	void ui_showPeerHistoryAsync(quint64 peerId, qint32 showAtMsgId);
	void ui_autoplayMediaInlineAsync(qint32 channelId, qint32 msgId);

private:

	void sendReadRequest(PeerData *peer, MsgId upTo);
	void channelWasRead(PeerData *peer, const MTPBool &result);
    void historyWasRead(PeerData *peer, const MTPmessages_AffectedMessages &result);
	bool readRequestFail(PeerData *peer, const RPCError &error);
	void readRequestDone(PeerData *peer);

	void messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result);
	void overviewLoaded(History *history, const MTPmessages_Messages &result, mtpRequestId req);

	bool _started = false;

	uint64 failedObjId = 0;
	QString failedFileName;
	void loadFailed(mtpFileLoader *loader, bool started, const char *retrySlot);

	SelectedItemSet _toForward;
	Text _toForwardFrom, _toForwardText;
	int32 _toForwardNameVersion = 0;

	QMap<WebPageId, bool> _webPagesUpdated;
	QTimer _webPageUpdater;

	SingleTimer _updateMutedTimer;

	enum GetChannelDifferenceFrom {
		GetChannelDifferenceFromUnknown,
		GetChannelDifferenceFromPtsGap,
		GetChannelDifferenceFromFail,
	};
	void getChannelDifference(ChannelData *channel, GetChannelDifferenceFrom from = GetChannelDifferenceFromUnknown);
	void gotDifference(const MTPupdates_Difference &diff);
	bool failDifference(const RPCError &e);
	void feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other);
	void gotState(const MTPupdates_State &state);
	void updSetState(int32 pts, int32 date, int32 qts, int32 seq);
	void gotChannelDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff);
	bool failChannelDifference(ChannelData *channel, const RPCError &err);
	void failDifferenceStartTimerFor(ChannelData *channel);

	void feedUpdateVector(const MTPVector<MTPUpdate> &updates, bool skipMessageIds = false);
	void feedMessageIds(const MTPVector<MTPUpdate> &updates);

	void deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result);
	struct DeleteAllFromUserParams {
		ChannelData *channel;
		UserData *from;
	};
	void deleteAllFromUserPart(DeleteAllFromUserParams params, const MTPmessages_AffectedHistory &result);

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result);
	bool usernameResolveFail(QString name, const RPCError &error);

	void inviteCheckDone(QString hash, const MTPChatInvite &invite);
	bool inviteCheckFail(const RPCError &error);
	QString _inviteHash;
	void inviteImportDone(const MTPUpdates &result);
	bool inviteImportFail(const RPCError &error);

	void hideAll();
	void showAll();

	void overviewPreloaded(PeerData *data, const MTPmessages_Messages &result, mtpRequestId req);
	bool overviewFailed(PeerData *data, const RPCError &error, mtpRequestId req);

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	int _dialogsWidth = st::dlgMinWidth;

	ChildWidget<DialogsWidget> _dialogs;
	ChildWidget<HistoryWidget> _history;
	ChildWidget<ProfileWidget> _profile = { nullptr };
	ChildWidget<OverviewWidget> _overview = { nullptr };
	ChildWidget<PlayerWidget> _player;
	ChildWidget<Window::TopBarWidget> _topBar;
	ConfirmBox *_forwardConfirm = nullptr; // for single column layout
	ChildWidget<HistoryHider> _hider = { nullptr };
	StackItems _stack;
	PeerData *_peerInStack = nullptr;
	MsgId _msgIdInStack = 0;

	int _playerHeight = 0;
	int _contentScrollAddToY = 0;

	ChildWidget<Dropdown> _mediaType;
	int32 _mediaTypeMask = 0;

	int32 updDate = 0;
	int32 updQts = -1;
	int32 updSeq = 0;
	SingleTimer noUpdatesTimer;

	bool ptsUpdated(int32 pts, int32 ptsCount);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdate &update);
	void ptsApplySkippedUpdates();
	PtsWaiter _ptsWaiter;

	typedef QMap<ChannelData*, uint64> ChannelGetDifferenceTime;
	ChannelGetDifferenceTime _channelGetDifferenceTimeByPts, _channelGetDifferenceTimeAfterFail;
	uint64 _getDifferenceTimeByPts = 0;
	uint64 _getDifferenceTimeAfterFail = 0;

	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, uint64 &curTime);

	SingleTimer _byPtsTimer;

	QMap<int32, MTPUpdates> _bySeqUpdates;
	SingleTimer _bySeqTimer;

	SingleTimer _byMinChannelTimer;

	mtpRequestId _onlineRequest = 0;
	SingleTimer _onlineTimer, _onlineUpdater, _idleFinishTimer;
	bool _lastWasOnline = false;
	uint64 _lastSetOnline = 0;
	bool _isIdle = false;

	QSet<PeerData*> updateNotifySettingPeers;
	SingleTimer updateNotifySettingTimer;

    typedef QMap<PeerData*, QPair<mtpRequestId, MsgId> > ReadRequests;
    ReadRequests _readRequests;
	typedef QMap<PeerData*, MsgId> ReadRequestsPending;
	ReadRequestsPending _readRequestsPending;

	typedef QMap<PeerData*, mtpRequestId> OverviewsPreload;
	OverviewsPreload _overviewPreload[OverviewCount], _overviewLoad[OverviewCount];

	int32 _failDifferenceTimeout = 1; // growing timeout for getDifference calls, if it fails
	typedef QMap<ChannelData*, int32> ChannelFailDifferenceTimeout;
	ChannelFailDifferenceTimeout _channelFailDifferenceTimeout; // growing timeout for getChannelDifference calls, if it fails
	SingleTimer _failDifferenceTimer;

	uint64 _lastUpdateTime = 0;
	bool _handlingChannelDifference = false;

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX = 0;
	int _cachedY = 0;
	SingleTimer _cacheBackgroundTimer;

	typedef QMap<ChannelData*, bool> UpdatedChannels;
	UpdatedChannels _updatedChannels;

	typedef QMap<MsgId, bool> ViewsIncrementMap;
	typedef QMap<PeerData*, ViewsIncrementMap> ViewsIncrement;
	ViewsIncrement _viewsIncremented, _viewsToIncrement;
	typedef QMap<PeerData*, mtpRequestId> ViewsIncrementRequests;
	ViewsIncrementRequests _viewsIncrementRequests;
	typedef QMap<mtpRequestId, PeerData*> ViewsIncrementByRequest;
	ViewsIncrementByRequest _viewsIncrementByRequest;
	SingleTimer _viewsIncrementTimer;
	void viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req);
	bool viewsIncrementFail(const RPCError &error, mtpRequestId req);

	std_::unique_ptr<App::WallPaper> _background;

	std_::unique_ptr<ApiWrap> _api;

};
