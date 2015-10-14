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

#include "dialogswidget.h"
#include "historywidget.h"
#include "profilewidget.h"
#include "overviewwidget.h"
#include "playerwidget.h"
#include "apiwrap.h"

class Window;
struct DialogRow;
class MainWidget;
class ConfirmBox;

class TopBarWidget : public TWidget, public Animated {
	Q_OBJECT

public:

	TopBarWidget(MainWidget *w);

	void enterEvent(QEvent *e);
	void enterFromChildEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void leaveToChildEvent(QEvent *e);
	void paintEvent(QPaintEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	bool animStep(float64 ms);
	void enableShadow(bool enable = true);

	void startAnim();
    void stopAnim();
	void showAll();
	void showSelected(uint32 selCount, bool canDelete = false);

	FlatButton *mediaTypeButton();

public slots:

	void onForwardSelection();
	void onDeleteSelection();
	void onClearSelection();
	void onInfoClicked();
	void onAddContact();
	void onEdit();
	void onDeleteContact();
	void onDeleteContactSure();
	void onDeleteAndExit();
	void onDeleteAndExitSure();

signals:

	void clicked();

private:

	MainWidget *main();
	anim::fvalue a_over;
	bool _drawShadow;

	PeerData *_selPeer;
	uint32 _selCount;
	bool _canDelete;
	QString _selStr;
	int32 _selStrLeft, _selStrWidth;
    
    bool _animating;

	FlatButton _clearSelection;
	FlatButton _forward, _delete;
	int32 _selectionButtonsWidth, _forwardDeleteWidth;

	FlatButton _info;
	FlatButton _edit, _leaveGroup, _addContact, _deleteContact;
	FlatButton _mediaType;

};

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
	StackItemHistory(PeerData *peer, MsgId msgId, QList<MsgId> replyReturns, bool kbWasHidden) : StackItem(peer),
msgId(msgId), replyReturns(replyReturns), kbWasHidden(kbWasHidden) {
	}
	StackItemType type() const {
		return HistoryStackItem;
	}
	MsgId msgId;
	QList<MsgId> replyReturns;
	bool kbWasHidden;
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

enum ForwardWhatMessages {
	ForwardSelectedMessages,
	ForwardContextMessage,
	ForwardPressedMessage,
	ForwardPressedLinkMessage
};

class MainWidget : public QWidget, public Animated, public RPCSender {
	Q_OBJECT

public:

	MainWidget(Window *window);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void updateWideMode();
	bool needBackButton();
	void showDialogs();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarShadowParams(int32 &x, float64 &o);
	TopBarWidget *topBar();

	PlayerWidget *player();
	int32 contentScrollAddToY() const;

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	bool animStep(float64 ms);

	void start(const MTPUser &user);

	void openLocalUrl(const QString &str);
	void openPeerByName(const QString &name, bool toProfile = false, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const MTPInputStickerSet &set);

	void startFull(const MTPVector<MTPUser> &users);
	bool started();
	void applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history = 0);
	void gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	bool failNotifySetting(MTPInputNotifyPeer peer, const RPCError &error);

	void updateNotifySetting(PeerData *peer, bool enabled);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialog(History *history);
	void dlgUpdated(DialogRow *row = 0);
	void dlgUpdated(History *row, MsgId msgId);

	void windowShown();

	void sentUpdatesReceived(uint64 randomId, const MTPUpdates &updates);
	void sentUpdatesReceived(const MTPUpdates &updates) {
		return sentUpdatesReceived(0, updates);
	}
	void inviteToChannelDone(ChannelData *channel, const MTPUpdates &updates);
	void msgUpdated(PeerId peer, const HistoryItem *msg);
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

	void confirmShareContact(bool ctrlShiftEnter, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo);
	void confirmSendImage(const ReadyLocalMedia &img);
	void confirmSendImageUncompressed(bool ctrlShiftEnter, MsgId replyTo);
	void cancelSendImage();

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	void addNewContact(int32 uid, bool show = true);

	bool isActive() const;
	bool historyIsActive() const;
	bool lastWasOnline() const;
	uint64 lastSetOnline() const;

	int32 dlgsWidth() const;

	void forwardLayer(int32 forwardSelected = 0); // -1 - send paths
	void deleteLayer(int32 selectedCount = -1); // -1 - context item, else selected, -2 - cancel upload
	void shareContactLayer(UserData *contact);
	void hiderLayer(HistoryHider *h);
	void noHider(HistoryHider *destroyed);
	bool onForward(const PeerId &peer, ForwardWhatMessages what);
	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer(bool withConfirm = false);
	void offerPeer(PeerId peer);
	void dialogsActivate();

	DragState getDragState(const QMimeData *mime);

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates);
	void deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result);
	void deleteMessages(PeerData *peer, const QVector<MTPint> &ids);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteConversation(PeerData *peer, bool deleteHistory = true);
	void clearHistory(PeerData *peer);
	void removeContact(UserData *user);

	void addParticipants(PeerData *chatOrChannel, const QVector<UserData*> &users);
	bool addParticipantFail(UserData *user, const RPCError &e);
	bool addParticipantsFail(const RPCError &e); // for multi invite in channels

	void kickParticipant(ChatData *chat, UserData *user);
	bool kickParticipantFail(ChatData *chat, const RPCError &e);

	void checkPeerHistory(PeerData *peer);
	void checkedHistory(PeerData *peer, const MTPmessages_Messages &result);

	bool sendPhotoFail(uint64 randomId, const RPCError &e);
	bool sendMessageFail(const RPCError &error);

	void forwardSelectedItems();
	void deleteSelectedItems();
	void clearSelectedItems();

	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();
    
    void sendMessage(History *history, const QString &text, MsgId replyTo, bool broadcast);
	void sendPreparedText(History *hist, const QString &text, MsgId replyTo, bool broadcast, WebPageId webPageId = 0);
	void saveRecentHashtags(const QString &text);
    
    void readServerHistory(History *history, bool force = true);

	uint64 animActiveTime(MsgId id) const;
	void stopAnimActive();

	void sendBotCommand(const QString &cmd, MsgId msgId);
	void insertBotCommand(const QString &cmd);

	void searchMessages(const QString &query, PeerData *inPeer);
	void preloadOverviews(PeerData *peer);
	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);
	void itemResized(HistoryItem *row, bool scrollToIt = false);

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
	void updateReplyTo();
	void updateBotKeyboard();

	void pushReplyReturn(HistoryItem *item);
	
	bool hasForwardingItems();
	void fillForwardingInfo(Text *&from, Text *&text, bool &serviceColor, ImagePtr &preview);
	void updateForwardingTexts();
	void cancelForwarding();
	void finishForwarding(History *hist, bool broadcast); // send them

	void audioMarkRead(AudioData *data);
	void videoMarkRead(VideoData *data);
	void mediaMarkRead(const HistoryItemsMap &items);

	void webPageUpdated(WebPageData *page);
	void updateMutedIn(int32 seconds);

	void updateStickers();
	void botCommandsChanged(UserData *bot);

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

	~MainWidget();

signals:

	void peerUpdated(PeerData *peer);
	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void peerPhotoChanged(PeerData *peer);
	void dialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);
	void dialogsUpdated();
	void showPeerAsync(quint64 peerId, qint32 showAtMsgId);
	void stickersUpdated();

public slots:

	void webPagesUpdate();

	void videoLoadProgress(mtpFileLoader *loader);
	void videoLoadFailed(mtpFileLoader *loader, bool started);
	void videoLoadRetry();
	void audioLoadProgress(mtpFileLoader *loader);
	void audioLoadFailed(mtpFileLoader *loader, bool started);
	void audioLoadRetry();
	void audioPlayProgress(const AudioMsgId &audioId);
	void documentLoadProgress(mtpFileLoader *loader);
	void documentLoadFailed(mtpFileLoader *loader, bool started);
	void documentLoadRetry();
	void documentPlayProgress(const SongMsgId &songId);
	void hidePlayer();

	void dialogsCancelled();

	void onParentResize(const QSize &newSize);
	void getDifference();
	void onGetDifferenceTimeByPts();
	void onGetDifferenceTimeAfterFail();
	void mtpPing();

	void updateOnline(bool gotOtherOffline = false);
	void checkIdleFinish();
	void updateOnlineDisplay();

	void showPeerHistory(quint64 peer, qint32 msgId, bool back = false);
	void onTopBarClick();
	void onHistoryShown(History *history, MsgId atMsgId);

	void searchInPeer(PeerData *peer);

	void onUpdateNotifySettings();

	void onPhotosSelect();
	void onVideosSelect();
	void onDocumentsSelect();
	void onAudiosSelect();
	void onLinksSelect();

	void onForwardCancel(QObject *obj = 0);

	void onResendAsDocument();
	void onCancelResend();

	void onCacheBackground();

	void onInviteImport();

	void onUpdateMuted();

	void onStickersInstalled(uint64 setId);
	void onFullPeerUpdated(PeerData *peer);

	void onViewsIncrement();
	void onActiveChannelUpdateFull();

private:

	void sendReadRequest(PeerData *peer, MsgId upTo);
	void channelWasRead(PeerData *peer, const MTPBool &result);
    void partWasRead(PeerData *peer, const MTPmessages_AffectedHistory &result);
	bool readRequestFail(PeerData *peer, const RPCError &error);
	void readRequestDone(PeerData *peer);

	void messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result);
	void overviewLoaded(History *h, const MTPmessages_Messages &msgs, mtpRequestId req);

	bool _started;

	uint64 failedObjId;
	QString failedFileName;
	void loadFailed(mtpFileLoader *loader, bool started, const char *retrySlot);

	QList<uint64> _resendImgRandomIds;

	SelectedItemSet _toForward;
	Text _toForwardFrom, _toForwardText;
	int32 _toForwardNameVersion;

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

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(QPair<bool, QString> toProfileStartToken, const MTPcontacts_ResolvedPeer &result);
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

	QPixmap _animCache, _bgAnimCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	int32 _dialogsWidth;

	DialogsWidget dialogs;
	HistoryWidget history;
	ProfileWidget *profile;
	OverviewWidget *overview;
	PlayerWidget _player;
	TopBarWidget _topBar;
	ConfirmBox *_forwardConfirm; // for narrow mode
	HistoryHider *_hider;
	StackItems _stack;
	PeerData *_peerInStack;
	MsgId _msgIdInStack;
	
	int32 _playerHeight;
	int32 _contentScrollAddToY;

	Dropdown _mediaType;
	int32 _mediaTypeMask;

	int32 updDate, updQts, updSeq;
	SingleTimer noUpdatesTimer;

	bool ptsUpdated(int32 pts, int32 ptsCount);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdate &update);
	void ptsApplySkippedUpdates();
	PtsWaiter _ptsWaiter;

	typedef QMap<ChannelData*, uint64> ChannelGetDifferenceTime;
	ChannelGetDifferenceTime _channelGetDifferenceTimeByPts, _channelGetDifferenceTimeAfterFail;
	uint64 _getDifferenceTimeByPts, _getDifferenceTimeAfterFail;

	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, uint64 &curTime);

	SingleTimer _byPtsTimer;

	QMap<int32, MTPUpdates> _bySeqUpdates;
	SingleTimer _bySeqTimer;

	mtpRequestId _onlineRequest;
	SingleTimer _onlineTimer, _onlineUpdater, _idleFinishTimer;
	bool _lastWasOnline;
	uint64 _lastSetOnline;
	bool _isIdle;

	QSet<PeerData*> updateNotifySettingPeers;
	SingleTimer updateNotifySettingTimer;
    
    typedef QMap<PeerData*, QPair<mtpRequestId, MsgId> > ReadRequests;
    ReadRequests _readRequests;
	typedef QMap<PeerData*, MsgId> ReadRequestsPending;
	ReadRequestsPending _readRequestsPending;

	typedef QMap<PeerData*, mtpRequestId> OverviewsPreload;
	OverviewsPreload _overviewPreload[OverviewCount], _overviewLoad[OverviewCount];

	int32 _failDifferenceTimeout; // growing timeout for getDifference calls, if it fails
	typedef QMap<ChannelData*, int32> ChannelFailDifferenceTimeout;
	ChannelFailDifferenceTimeout _channelFailDifferenceTimeout; // growing timeout for getChannelDifference calls, if it fails
	SingleTimer _failDifferenceTimer;

	uint64 _lastUpdateTime;
	bool _handlingChannelDifference;

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX, _cachedY;
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

	App::WallPaper *_background;

	ApiWrap *_api;

};
