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

#include <QtWidgets/QWidget>
#include "gui/flatbutton.h"

#include "dialogswidget.h"
#include "historywidget.h"
#include "profilewidget.h"
#include "overviewwidget.h"

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
	void showSelected(uint32 selCount);

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

	uint32 _selCount;
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
	StackItemHistory(PeerData *peer, int32 lastWidth, int32 lastScrollTop) : StackItem(peer), lastWidth(lastWidth), lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return HistoryStackItem;
	}
	int32 lastWidth, lastScrollTop;
};

class StackItemProfile : public StackItem {
public:
	StackItemProfile(PeerData *peer, int32 lastScrollTop, bool allMediaShown) : StackItem(peer), lastScrollTop(lastScrollTop), allMediaShown(allMediaShown) {
	}
	StackItemType type() const {
		return ProfileStackItem;
	}
	int32 lastScrollTop;
	bool allMediaShown;
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

class MainWidget : public QWidget, public Animated, public RPCSender {
	Q_OBJECT

public:

	MainWidget(Window *window);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void updateWideMode();
	bool needBackButton();
	void onShowDialogs();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarShadowParams(int32 &x, float64 &o);
	TopBarWidget *topBar();

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	bool animStep(float64 ms);

	void start(const MTPUser &user);
	void openLocalUrl(const QString &str);
	void openUserByName(const QString &name);
	void startFull(const MTPVector<MTPUser> &users);
	bool started();
	void applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history = 0);
	void gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	bool failNotifySetting(MTPInputNotifyPeer peer);

	void updateNotifySetting(PeerData *peer, bool enabled);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialogAtTop(History *history, int32 unreadCount);
	void dlgUpdated(DialogRow *row);
	void dlgUpdated(History *row);

	void windowShown();

	void sentDataReceived(uint64 randomId, const MTPmessages_SentMessage &data);
	void sentFullDataReceived(uint64 randomId, const MTPmessages_StatedMessage &result); // randomId = 0 - new message, <> 0 - already added new message
	void sentFullDatasReceived(const MTPmessages_StatedMessages &result);
	void forwardDone(PeerId peer, const MTPmessages_StatedMessages &result);
	void msgUpdated(PeerId peer, const HistoryItem *msg);
	void historyToDown(History *hist);
	void dialogsToUp();
	void newUnreadMsg(History *history, MsgId msgId);
	void updUpdated(int32 pts, int32 seq);
	void historyWasRead();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	PeerData *historyPeer();
	PeerData *peer();
	PeerData *activePeer();
	MsgId activeMsgId();
	PeerData *profilePeer();
	bool mediaTypeSwitch();
	void showPeerProfile(PeerData *peer, bool back = false, int32 lastScrollTop = -1, bool allMediaShown = false);
	void showMediaOverview(PeerData *peer, MediaOverviewType type, bool back = false, int32 lastScrollTop = -1);
	void showBackFromStack();
	QRect historyRect() const;

	void confirmShareContact(bool ctrlShiftEnter, const QString &phone, const QString &fname, const QString &lname);
	void confirmSendImage(const ReadyLocalMedia &img);
	void confirmSendImageUncompressed(bool ctrlShiftEnter);
	void cancelSendImage();

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	void addNewContact(int32 uid, bool show = true);

	bool isActive() const;
	bool historyIsActive() const;

	int32 dlgsWidth() const;

	void forwardLayer(int32 forwardSelected = 0); // -1 - send paths
	void deleteLayer(int32 selectedCount = -1); // -1 - context item, else selected, -2 - cancel upload
	void shareContactLayer(UserData *contact);
	void hiderLayer(HistoryHider *h);
	void noHider(HistoryHider *destroyed);
	mtpRequestId onForward(const PeerId &peer, bool forwardSelected);
	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);
	bool selectingPeer();
	void offerPeer(PeerId peer);
	void focusPeerSelect();
	void dialogsActivate();

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistory(PeerData *peer, const MTPmessages_StatedMessage &result);
	void deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteHistoryAndContact(UserData *user, const MTPcontacts_Link &result);
	void clearHistory(PeerData *peer);
	void removeContact(UserData *user);

	void addParticipants(ChatData *chat, const QVector<UserData*> &users);
	void addParticipantDone(ChatData *chat, const MTPmessages_StatedMessage &result);
	bool addParticipantFail(ChatData *chat, const RPCError &e);

	void kickParticipant(ChatData *chat, UserData *user);
	void kickParticipantDone(ChatData *chat, const MTPmessages_StatedMessage &result);
	bool kickParticipantFail(ChatData *chat, const RPCError &e);

	void checkPeerHistory(PeerData *peer);
	void checkedHistory(PeerData *peer, const MTPmessages_Messages &result);

	bool sendPhotoFailed(uint64 randomId, const RPCError &e);

	void forwardSelectedItems();
	void deleteSelectedItems();
	void clearSelectedItems();

	DialogsIndexed &contactsList();
    
    void sendMessage(History *history, const QString &text);
	void sendPreparedText(History *hist, const QString &text);
    
    void readServerHistory(History *history, bool force = true);

	uint64 animActiveTime() const;
	void stopAnimActive();

	void searchMessages(const QString &query);
	void preloadOverviews(PeerData *peer);
	void mediaOverviewUpdated(PeerData *peer);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);
	void itemResized(HistoryItem *row);

	void loadMediaBack(PeerData *peer, MediaOverviewType type, bool many = false);
	void peerUsernameChanged(PeerData *peer);

	void checkLastUpdate(bool afterSleep);
	void showAddContact();
	void showNewGroup();

	void serviceNotification(const QString &msg, const MTPMessageMedia &media, bool unread);
	void serviceHistoryDone(const MTPmessages_Messages &msgs);
	bool serviceHistoryFail(const RPCError &error);

	~MainWidget();

signals:

	void peerUpdated(PeerData *peer);
	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void peerPhotoChanged(PeerData *peer);
	void dialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);
	void dialogToTop(const History::DialogLinks &links);
	void dialogsUpdated();
	void showPeerAsync(quint64 peer, qint32 msgId, bool back, bool force);

public slots:

	void videoLoadProgress(mtpFileLoader *loader);
	void videoLoadFailed(mtpFileLoader *loader, bool started);
	void videoLoadRetry();
	void audioLoadProgress(mtpFileLoader *loader);
	void audioLoadFailed(mtpFileLoader *loader, bool started);
	void audioLoadRetry();
	void audioPlayProgress(AudioData *audio);
	void documentLoadProgress(mtpFileLoader *loader);
	void documentLoadFailed(mtpFileLoader *loader, bool started);
	void documentLoadRetry();

	void setInnerFocus();
	void dialogsCancelled();

	void onParentResize(const QSize &newSize);
	void getDifference();
	void getDifferenceForce();

	void setOnline(int windowState = -1);
	void mainStateChanged(Qt::WindowState state);
	void updateOnlineDisplay();

	void showPeer(quint64 peer, qint32 msgId = 0, bool back = false, bool force = false); // PeerId, MsgId
	void onTopBarClick();
	void onPeerShown(PeerData *peer);

	void onUpdateNotifySettings();

	void onPhotosSelect();
	void onVideosSelect();
	void onDocumentsSelect();
	void onAudiosSelect();

	void onForwardCancel(QObject *obj = 0);

	void onResendAsDocument();
	void onCancelResend();

private:

    void partWasRead(PeerData *peer, const MTPmessages_AffectedHistory &result);
	void photosLoaded(History *h, const MTPmessages_Messages &msgs, mtpRequestId req);

	bool _started;

	uint64 failedObjId;
	QString failedFileName;
	void loadFailed(mtpFileLoader *loader, bool started, const char *retrySlot);

	QList<uint64> _resendImgRandomIds;

	void gotDifference(const MTPupdates_Difference &diff);
	bool failDifference(const RPCError &e);
	void feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other);
	void gotState(const MTPupdates_State &state);
	void updSetState(int32 pts, int32 date, int32 qts, int32 seq);

	void feedUpdates(const MTPVector<MTPUpdate> &updates, bool skipMessageIds = false);
	void feedMessageIds(const MTPVector<MTPUpdate> &updates);
	void feedUpdate(const MTPUpdate &update);

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	void handleUpdates(const MTPUpdates &updates);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(const MTPUser &user);
	bool usernameResolveFail(QString name, const RPCError &error);

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
	TopBarWidget _topBar;
	ConfirmBox *_forwardConfirm; // for narrow mode
	HistoryHider *hider;
	StackItems _stack;
	QPixmap profileAnimCache;

	Dropdown _mediaType;
	int32 _mediaTypeMask;

	int updPts, updDate, updQts, updSeq;
	bool updInited;
	SingleTimer noUpdatesTimer;

	mtpRequestId onlineRequest;
	SingleTimer onlineTimer;
	SingleTimer onlineUpdater;

	QSet<PeerData*> updateNotifySettingPeers;
	SingleTimer updateNotifySettingTimer;
    
    typedef QMap<PeerData*, mtpRequestId> ReadRequests;
    ReadRequests _readRequests;

	typedef QMap<PeerData*, mtpRequestId> OverviewsPreload;
	OverviewsPreload _overviewPreload[OverviewCount], _overviewLoad[OverviewCount];

	QMap<int32, MTPUpdates> _bySeqUpdates;
	QMap<int32, MTPmessages_SentMessage> _bySeqSentMessage;
	QMap<int32, MTPmessages_StatedMessage> _bySeqStatedMessage;
	QMap<int32, MTPmessages_StatedMessages> _bySeqStatedMessages;
	QMap<int32, int32> _bySeqPart;
	SingleTimer _bySeqTimer;

	int32 _failDifferenceTimeout; // growing timeout for getDifference calls, if it fails
	SingleTimer _failDifferenceTimer;

	uint64 _lastUpdateTime;
};
