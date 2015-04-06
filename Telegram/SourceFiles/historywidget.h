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

#include "localimageloader.h"
#include "gui/boxshadow.h"

#include "dropdown.h"

enum DragState {
	DragStateNone       = 0x00,
	DragStateFiles      = 0x01,
	DragStatePhotoFiles = 0x02,
	DragStateImage      = 0x03,
};

class HistoryWidget;
class HistoryList : public QWidget {
	Q_OBJECT

public:

	HistoryList(HistoryWidget *historyWidget, ScrollArea *scroll, History *history);

	void messagesReceived(const QVector<MTPMessage> &messages);
	void messagesReceivedDown(const QVector<MTPMessage> &messages);

	bool event(QEvent *e); // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void mouseDoubleClickEvent(QMouseEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);

	QString getSelectedText() const;

	void dragActionStart(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionUpdate(const QPoint &screenPos);
	void dragActionFinish(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionCancel();

	void touchScrollUpdated(const QPoint &screenPos);
	QPoint mapMouseToItem(QPoint p, HistoryItem *item);

	int32 recountHeight(bool dontRecountText);
	void updateSize();

	void updateMsg(const HistoryItem *msg);

	bool canCopySelected() const;
	bool canDeleteSelected() const;

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);
	void selectItem(HistoryItem *item);

	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);

	~HistoryList();
	
public slots:

	void onUpdateSelected();
	void onParentGeometryChanged();

	void showLinkTip();

	void openContextUrl();
	void copyContextUrl();
	void saveContextImage();
	void copyContextImage();
	void cancelContextDownload();
	void showContextInFolder();
	void openContextFile();
	void saveContextFile();
	void copyContextText();
	void copySelectedText();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();

private:

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void adjustCurrent(int32 y);
	HistoryItem *prevItem(HistoryItem *item);
	HistoryItem *nextItem(HistoryItem *item);
	void updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force = false);
	void applyDragSelection();

	History *hist;
	HistoryWidget *historyWidget;
	ScrollArea *scrollArea;
	int32 currentBlock, currentItem;

	QTimer linkTipTimer;

	Qt::CursorShape _cursor;
	typedef QMap<HistoryItem*, uint32> SelectedItems;
	SelectedItems _selected;
	enum DragAction {
		NoDrag        = 0x00,
		PrepareDrag   = 0x01,
		Dragging      = 0x02,
		PrepareSelect = 0x03,
		Selecting     = 0x04,
	};
	DragAction _dragAction;
	TextSelectType _dragSelType;
	QPoint _dragStartPos, _dragPos;
	HistoryItem *_dragItem;
	uint16 _dragSymbol;
	bool _dragWasInactive;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	TextLinkPtr _contextMenuLnk;

	HistoryItem *_dragSelFrom, *_dragSelTo;
	bool _dragSelecting;

	bool _touchScroll, _touchSelect, _touchInProgress;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;
	
	TouchScrollState _touchScrollState;
	bool _touchPrevPosValid, _touchWaitingAcceleration;
	QPoint _touchSpeed;
	uint64 _touchSpeedTime, _touchAccelerationTime, _touchTime;
	QTimer _touchScrollTimer;

	ContextMenu *_menu;

};

class MessageField : public FlatTextarea {
	Q_OBJECT

public:
	MessageField(HistoryWidget *history, const style::flatTextarea &st, const QString &ph = QString(), const QString &val = QString());
	void dropEvent(QDropEvent *e);
	void resizeEvent(QResizeEvent *e);
	bool canInsertFromMimeData(const QMimeData *source) const;
	void insertFromMimeData(const QMimeData *source);

	void focusInEvent(QFocusEvent *e);

public slots:

	void onChange();
	void onEmojiInsert(EmojiPtr emoji);

signals:

	void resized();
	void focused();

private:
	HistoryWidget *history;
};

class HistoryHider : public QWidget, public Animated {
	Q_OBJECT

public:

	HistoryHider(MainWidget *parent, bool forwardSelected); // forward messages
	HistoryHider(MainWidget *parent, UserData *sharedContact); // share contact
	HistoryHider(MainWidget *parent); // send path from command line argument

	bool animStep(float64 ms);
	bool withConfirm() const;

	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	bool offerPeer(PeerId peer);
	QString offeredText() const;

	bool wasOffered() const;

	void forwardDone();

	~HistoryHider();

public slots:

	void startHide();
	void forward();

signals:

	void forwarded();

private:

	void init();
	MainWidget *parent();

	UserData *_sharedContact;
	bool _forwardSelected, _sendPath;

	FlatButton forwardButton;
	FlatButton cancelButton;
	PeerData *offered;
	anim::fvalue aOpacity;
	anim::transition aOpacityFunc;
	QRect box;
	bool hiding;

	mtpRequestId _forwardRequest;

	int32 _chooseWidth;

	Text toText;
	int32 toTextWidth;
	QPixmap cacheForAnim;

	BoxShadow shadow;

};

class HistoryWidget : public QWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	HistoryWidget(QWidget *parent);

	void start();

	void messagesReceived(const MTPmessages_Messages &messages, mtpRequestId requestId);

	void windowShown();
	bool isActive() const;

	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void paintEvent(QPaintEvent *e);
    void dragEnterEvent(QDragEnterEvent *e);
	void dragLeaveEvent(QDragLeaveEvent *e);
	void leaveEvent(QEvent *e);
    void dropEvent(QDropEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void updateTopBarSelection();
	void checkMentionDropdown();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarShadowParams(int32 &x, float64 &o);
	void topBarClick();

	void loadMessages();
	void loadMessagesDown();
	void loadMessagesAround();
	void peerMessagesUpdated(PeerId peer);
	void peerMessagesUpdated();

	void msgUpdated(PeerId peer, const HistoryItem *msg);
	void newUnreadMsg(History *history, HistoryItem *item);
	void historyToDown(History *history);
	void historyWasRead(bool force = true);

	QRect historyRect() const;

	void updateTyping(bool typing = true);
//	void updateStickerPan();
	void updateRecentStickers();
	void typingDone(const MTPBool &result, mtpRequestId req);

	void destroyData();
	void uploadImage(const QImage &img, bool withText = false, const QString &source = QString());
	void uploadFile(const QString &file, bool withText = false); // with confirmation
	void shareContactConfirmation(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool withText = false);
	void uploadConfirmImageUncompressed(bool ctrlShiftEnter, MsgId replyTo);
	void uploadMedias(const QStringList &files, ToPrepareMediaType type);
	void uploadMedia(const QByteArray &fileContent, ToPrepareMediaType type, PeerId peer = 0);
	void confirmShareContact(bool ctrlShiftEnter, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo);
	void confirmSendImage(const ReadyLocalMedia &img);
	void cancelSendImage();

	void checkUnreadLoaded(bool checkOnlyShow = false);
	void updateControlsVisibility();
	void updateOnlineDisplay(int32 x, int32 w);
	void updateOnlineDisplayTimer();

//	mtpRequestId onForward(const PeerId &peer, SelectedItemSet toForward);
	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);

	void shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId = 0);

	PeerData *peer() const;
	PeerData *activePeer() const;
	MsgId activeMsgId() const;
	int32 lastWidth() const;
	int32 lastScrollTop() const;

	void animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false);
	bool animStep(float64 ms);
	void animStop();

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);
    
	QString prepareMessage(QString text);

	uint64 animActiveTime() const;
	void stopAnimActive();

	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);
	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);
	void itemResized(HistoryItem *item, bool scrollToIt);

	void updateScrollColors();

	MsgId replyToId() const;
	void updateReplyTo(bool force = false);
	void cancelReply();
	void updateForwarding(bool force = false);
	void cancelForwarding(); // called by MainWidget

	void clearReplyReturns();
	void pushReplyReturn(HistoryItem *item);
	QList<MsgId> replyReturns();
	void setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns);
	void calcNextReplyReturn();

	void updatePreview();
	void previewCancel();

	~HistoryWidget();

signals:

	void cancelled();
	void peerShown(PeerData *);

public slots:

	void onCancel();
	void onReplyToMessage();
	void onReplyForwardPreviewCancel();

	void onPreviewParse();
	void onPreviewCheck();
	void onPreviewTimeout();

	void peerUpdated(PeerData *data);
	void onPeerLoaded(PeerData *data);

	void cancelTyping();

	void onPhotoUploaded(MsgId msgId, const MTPInputFile &file);
	void onDocumentUploaded(MsgId msgId, const MTPInputFile &file);
	void onThumbDocumentUploaded(MsgId msgId, const MTPInputFile &file, const MTPInputFile &thumb);

	void onDocumentProgress(MsgId msgId);

	void onDocumentFailed(MsgId msgId);

	void onListScroll();
	void onHistoryToEnd();
	void onSend(bool ctrlShiftEnter = false, MsgId replyTo = -1);

	void onPhotoSelect();
	void onDocumentSelect();
	void onPhotoDrop(QDropEvent *e);
	void onDocumentDrop(QDropEvent *e);

	void onPhotoReady();
	void onSendConfirmed();
	void onSendCancelled();
	void onPhotoFailed(quint64 id);
	void showPeer(const PeerId &peer, MsgId msgId = 0, bool force = false, bool leaveActive = false);
	void clearLoadingAround();
	void activate();
	void onTextChange();

	void onStickerSend(DocumentData *sticker);

	void onVisibleChanged();

	void deleteMessage();
	void forwardMessage();
	void selectMessage();

	void onFieldFocused();
	void onFieldResize();
	void onFieldCursorChanged();
	void onScrollTimer();

	void onForwardSelected();
	void onDeleteSelected();
	void onDeleteSelectedSure();
	void onDeleteContextSure();
	void onClearSelected();

	void onAnimActiveStep();

	void onDraftSaveDelayed();
	void onDraftSave(bool delayed = false);

	void updateStickers();

private:

	MsgId _replyToId;
	HistoryItem *_replyTo;
	Text _replyToName, _replyToText;
	int32 _replyToNameVersion;
	IconedButton _replyForwardPreviewCancel;
	void updateReplyToName();
	void drawFieldBackground(QPainter &p);

	QString _previewLinks;
	WebPageData *_previewData;
	typedef QMap<QString, WebPageId> PreviewCache;
	PreviewCache _previewCache;
	mtpRequestId _previewRequest;
	Text _previewTitle, _previewDescription;
	SingleTimer _previewTimer;
	bool _previewCancelled;
	void gotPreview(QString links, const MTPMessageMedia &media, mtpRequestId req);

	HistoryItem *_replyReturn;
	QList<MsgId> _replyReturns;

	bool messagesFailed(const RPCError &error, mtpRequestId requestId);
	void updateListSize(int32 addToY = 0, bool initial = false, bool loadedDown = false, HistoryItem *resizedItem = 0, bool scrollToIt = false);
	void addMessagesToFront(const QVector<MTPMessage> &messages);
	void addMessagesToBack(const QVector<MTPMessage> &messages);

	void stickersGot(const MTPmessages_AllStickers &stickers);
	bool stickersFailed(const RPCError &error);

	uint64 _lastStickersUpdate;
	mtpRequestId _stickersUpdateRequest;

	void writeDraft(MsgId *replyTo = 0, const QString *text = 0, const MessageCursor *cursor = 0, bool *previewCancelled = 0);
	void setFieldText(const QString &text);

	QStringList getMediasFromMime(const QMimeData *d);
	DragState getDragState(const QMimeData *d);

	void updateDragAreas();

	bool _loadingMessages;
	int32 histRequestsCount;
	PeerData *histPeer;
	History *_activeHist;
	MTPinputPeer histInputPeer;
	mtpRequestId histPreloading, histPreloadingDown;
	QVector<MTPMessage> histPreload, histPreloadDown;

	int32 _loadingAroundId;
	mtpRequestId _loadingAroundRequest;

	ScrollArea _scroll;
	HistoryList *_list;
	History *hist;
	bool _histInited; // initial updateListSize() called

	IconedButton _toHistoryEnd;

	MentionsDropdown _attachMention;

	FlatButton _send;
	IconedButton _attachDocument, _attachPhoto, _attachEmoji;
	MessageField _field;

	Dropdown _attachType;
	EmojiPan _emojiPan;
//	StickerPan _stickerPan;
	DragState _attachDrag;
	DragArea _attachDragDocument, _attachDragPhoto;

	int32 _selCount; // < 0 - text selected, focus list, not _field

	LocalImageLoader imageLoader;
	bool _synthedTextUpdate;

	int64 serviceImageCacheSize;
	QImage confirmImage;
	PhotoId confirmImageId;
	bool confirmWithText;
	QString confirmSource;

	QString titlePeerText;
	int32 titlePeerTextWidth;

	bool hiderOffered;

	QPixmap _animCache, _bgAnimCache, _animTopBarCache, _bgAnimTopBarCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	QTimer _scrollTimer;
	int32 _scrollDelta;

	QTimer _animActiveTimer;
	float64 _animActiveStart;

	mtpRequestId _typingRequest;
	QTimer _typingStopTimer;

	uint64 _saveDraftStart;
	bool _saveDraftText;
	QTimer _saveDraftTimer;

};

