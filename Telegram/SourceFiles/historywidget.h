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
#include "gui/boxshadow.h"

#include "dropdown.h"

enum DragState {
	DragStateNone       = 0x00,
	DragStateFiles      = 0x01,
	DragStatePhotoFiles = 0x02,
	DragStateImage      = 0x03,
};

class HistoryWidget;
class HistoryInner : public TWidget, public AbstractTooltipShower {
	Q_OBJECT

public:

	HistoryInner(HistoryWidget *historyWidget, ScrollArea *scroll, History *history);

	void messagesReceived(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed);
	void messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed);

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

	void recountHeight();
	void updateSize();

	void repaintItem(const HistoryItem *item);

	bool canCopySelected() const;
	bool canDeleteSelected() const;

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);
	void selectItem(HistoryItem *item);

	void itemRemoved(HistoryItem *item);

	void updateBotInfo(bool recount = true);

	bool wasSelectedText() const;
	void setFirstLoading(bool loading);

	HistoryItem *atTopImportantMsg(int32 top, int32 height, int32 &bottomUnderScrollTop) const;

	// updates history->scrollTopItem/scrollTopOffset
	void visibleAreaUpdated(int top, int bottom);

	int historyHeight() const;
	int historyScrollTop() const;
	int migratedTop() const;
	int historyTop() const;
	int historyDrawTop() const;
	int itemTop(const HistoryItem *item) const; // -1 if should not be visible, -2 if bad history()

	void notifyIsBotChanged();
	void notifyMigrateUpdated();

	// AbstractTooltipShower
	virtual QString tooltipText() const;
	virtual QPoint tooltipPos() const;

	~HistoryInner();

public slots:

	void onUpdateSelected();
	void onParentGeometryChanged();

	void copyContextUrl();
	void saveContextImage();
	void copyContextImage();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextFile();
	void saveContextGif();
	void copyContextText();
	void copySelectedText();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();
	void onDragExec();

private:

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void adjustCurrent(int32 y) const;
	void adjustCurrent(int32 y, History *history) const;
	HistoryItem *prevItem(HistoryItem *item);
	HistoryItem *nextItem(HistoryItem *item);
	void updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force = false);

	PeerData *_peer;
	History *_migrated, *_history;
	int32 _historyOffset, _historySkipHeight; // height of first date and first sys msg

	BotInfo *_botInfo;
	int32 _botDescWidth, _botDescHeight;
	QRect _botDescRect;

	HistoryWidget *_widget;
	ScrollArea *_scroll;
	mutable History *_curHistory;
	mutable int32 _curBlock, _curItem;

	bool _firstLoading;

	Qt::CursorShape _cursor;
	typedef QMap<HistoryItem*, uint32> SelectedItems;
	SelectedItems _selected;
	void applyDragSelection();
	void applyDragSelection(SelectedItems *toItems) const;
	void addSelectionRange(SelectedItems *toItems, int32 fromblock, int32 fromitem, int32 toblock, int32 toitem, History *h) const;

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
	HistoryCursorState _dragCursorState;
	uint16 _dragSymbol;
	bool _dragWasInactive;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	TextLinkPtr _contextMenuLnk;

	HistoryItem *_dragSelFrom, *_dragSelTo;
	bool _dragSelecting;
	bool _wasSelectedText; // was some text selected in current drag action

	bool _touchScroll, _touchSelect, _touchInProgress;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

	TouchScrollState _touchScrollState;
	bool _touchPrevPosValid, _touchWaitingAcceleration;
	QPoint _touchSpeed;
	uint64 _touchSpeedTime, _touchAccelerationTime, _touchTime;
	QTimer _touchScrollTimer;

	PopupMenu *_menu;

	int _visibleAreaTop = 0;

};

class MessageField : public FlatTextarea {
	Q_OBJECT

public:
	MessageField(HistoryWidget *history, const style::flatTextarea &st, const QString &ph = QString(), const QString &val = QString());
	void dropEvent(QDropEvent *e);
	bool canInsertFromMimeData(const QMimeData *source) const;
	void insertFromMimeData(const QMimeData *source);

	void focusInEvent(QFocusEvent *e);

	bool hasSendText() const;

public slots:

	void onEmojiInsert(EmojiPtr emoji);

signals:

	void focused();

private:
	HistoryWidget *history;

};

class HistoryWidget;
class ReportSpamPanel : public TWidget {
	Q_OBJECT

public:

	ReportSpamPanel(HistoryWidget *parent);

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	void setReported(bool reported, PeerData *onPeer);

signals:

	void hideClicked();
	void reportClicked();
	void clearClicked();

private:

	FlatButton _report, _hide;
	LinkButton _clear;

};

class BotKeyboard : public TWidget, public AbstractTooltipShower {
	Q_OBJECT

public:

	BotKeyboard();

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	bool updateMarkup(HistoryItem *last);
	bool hasMarkup() const;
	bool forceReply() const;

	void step_selected(uint64 ms, bool timer);
	void resizeToWidth(int32 width, int32 maxOuterHeight);

	bool maximizeSize() const;
	bool singleUse() const;

	FullMsgId forMsgId() const {
		return _wasForMsgId;
	}

	// AbstractTooltipShower
	virtual QString tooltipText() const;
	virtual QPoint tooltipPos() const;

public slots:

	void updateSelected();

private:

	void updateStyle(int32 w = -1);
	void clearSelection();

	FullMsgId _wasForMsgId;
	int32 _height, _maxOuterHeight;
	bool _maximizeSize, _singleUse, _forceReply;

	QPoint _lastMousePos;
	struct Button {
		Button(const QString &str = QString()) : cmd(str), text(1), cwidth(0), hover(0), full(true) {
		}
		QRect rect;
		QString cmd;
		Text text;
		int32 cwidth;
		float64 hover;
		bool full;
	};
	int32 _sel, _down;
	QList<QList<Button> > _btns;

	typedef QMap<int32, uint64> Animations;
	Animations _animations;
	Animation _a_selected;

	const style::botKeyboardButton *_st;

};

class HistoryHider : public TWidget {
	Q_OBJECT

public:

	HistoryHider(MainWidget *parent, bool forwardSelected); // forward messages
	HistoryHider(MainWidget *parent, UserData *sharedContact); // share contact
	HistoryHider(MainWidget *parent); // send path from command line argument
	HistoryHider(MainWidget *parent, const QString &url, const QString &text); // share url

	void step_appearance(float64 ms, bool timer);
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

	QString _shareUrl, _shareText;

	BoxButton _send, _cancel;
	PeerData *offered;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QRect box;
	bool hiding;

	mtpRequestId _forwardRequest;

	int32 _chooseWidth;

	Text toText;
	int32 toTextWidth;
	QPixmap cacheForAnim;

	BoxShadow shadow;

};

class CollapseButton : public FlatButton {
public:

	CollapseButton(QWidget *parent);
	void paintEvent(QPaintEvent *e);

};

class SilentToggle : public FlatCheckbox, public AbstractTooltipShower {
public:

	SilentToggle(QWidget *parent);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	// AbstractTooltipShower
	virtual QString tooltipText() const;
	virtual QPoint tooltipPos() const;

};

enum TextUpdateEventsFlags {
	TextUpdateEventsSaveDraft  = 0x01,
	TextUpdateEventsSendTyping = 0x02,
};

class HistoryWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	HistoryWidget(QWidget *parent);

	void start();

	void messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId);
	void historyLoaded();

	void windowShown();
	bool isActive() const;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void leaveEvent(QEvent *e) override;
    void dropEvent(QDropEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveToChildEvent(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void updateTopBarSelection();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	void loadMessages();
	void loadMessagesDown();
	void firstLoadMessages();
	void delayedShowAt(MsgId showAtMsgId);
	void peerMessagesUpdated(PeerId peer);
	void peerMessagesUpdated();

	void newUnreadMsg(History *history, HistoryItem *item);
	void historyToDown(History *history);
	void historyWasRead(bool force = true);
	void historyCleared(History *history);

	QRect historyRect() const;

	void updateSendAction(History *history, SendActionType type, int32 progress = 0);
	void cancelSendAction(History *history, SendActionType type);

	void updateRecentStickers();
	void stickersInstalled(uint64 setId);
	void sendActionDone(const MTPBool &result, mtpRequestId req);

	void destroyData();

	void updateFieldPlaceholder();
	void updateInlineBotQuery();
	void updateStickersByEmoji();

	void uploadImage(const QImage &img, PrepareMediaType type, FileLoadForceConfirmType confirm = FileLoadNoForceConfirm, const QString &source = QString(), bool withText = false);
	void uploadFile(const QString &file, PrepareMediaType type, FileLoadForceConfirmType confirm = FileLoadNoForceConfirm, bool withText = false); // with confirmation
	void uploadFiles(const QStringList &files, PrepareMediaType type);
	void uploadFileContent(const QByteArray &fileContent, PrepareMediaType type);
	void shareContactWithConfirm(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool withText = false);

	void confirmSendFile(const FileLoadResultPtr &file, bool ctrlShiftEnter);
	void cancelSendFile(const FileLoadResultPtr &file);
	void confirmShareContact(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool ctrlShiftEnter);
	void cancelShareContact();

	void updateControlsVisibility();
	void updateOnlineDisplay(int32 x, int32 w);
	void updateOnlineDisplayTimer();

	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);

	void shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId = 0);

	History *history() const;
	PeerData *peer() const;
	void setMsgId(MsgId showAtMsgId);
	MsgId msgId() const;
	HistoryItem *atTopImportantMsg(int32 &bottomUnderScrollTop) const;

	void animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false);
	void step_show(float64 ms, bool timer);
	void animStop();

	void updateAdaptiveLayout();
	void doneShow();

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);

	uint64 animActiveTimeStart(const HistoryItem *msg) const;
	void stopAnimActive();

	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);
	void itemRemoved(HistoryItem *item);
	void itemEdited(HistoryItem *item);

	void updateScrollColors();

	MsgId replyToId() const;
	void messageDataReceived(ChannelData *channel, MsgId msgId);
	bool lastForceReplyReplied(const FullMsgId &replyTo = FullMsgId(NoChannel, -1)) const;
	void cancelReply(bool lastKeyboardUsed = false);
	void cancelEdit();
	void updateForwarding(bool force = false);
	void cancelForwarding(); // called by MainWidget

	void clearReplyReturns();
	void pushReplyReturn(HistoryItem *item);
	QList<MsgId> replyReturns();
	void setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns);
	void calcNextReplyReturn();

	void updatePreview();
	void previewCancel();

	void step_record(float64 ms, bool timer);
	void step_recording(float64 ms, bool timer);
	void stopRecording(bool send);

	void onListEscapePressed();

	void sendBotCommand(const QString &cmd, MsgId replyTo);
	bool insertBotCommand(const QString &cmd, bool specialGif);

	bool eventFilter(QObject *obj, QEvent *e) override;
	void updateBotKeyboard(History *h = 0);

	DragState getDragState(const QMimeData *d);

	void fastShowAtEnd(History *h);
	void applyDraft(bool parseLinks = true);
	void showHistory(const PeerId &peer, MsgId showAtMsgId, bool reload = false);
	void clearDelayedShowAt();
	void clearAllLoadRequests();

	void contactsReceived();
	void updateToEndVisibility();
	void updateCollapseCommentsVisibility();

	void updateAfterDrag();
	void ctrlEnterSubmitUpdated();

	void setInnerFocus();
	bool canSendMessages(PeerData *peer) const;

	void updateNotifySettings();

	void saveGif(DocumentData *doc);

	bool contentOverlapped(const QRect &globalRect);

	void grabStart() override {
		_sideShadow.hide();
		_inGrab = true;
		resizeEvent(0);
	}
	void grabFinish() override {
		_sideShadow.setVisible(!Adaptive::OneColumn());
		_inGrab = false;
		resizeEvent(0);
	}

	bool isItemVisible(HistoryItem *item);

	void ui_repaintHistoryItem(const HistoryItem *item);
	void ui_repaintInlineItem(const LayoutInlineItem *gif);
	bool ui_isInlineItemVisible(const LayoutInlineItem *layout);
	bool ui_isInlineItemBeingChosen();

	void notify_historyItemLayoutChanged(const HistoryItem *item);
	void notify_botCommandsChanged(UserData *user);
	void notify_inlineBotRequesting(bool requesting);
	void notify_userIsBotChanged(UserData *user);
	void notify_migrateUpdated(PeerData *peer);
	void notify_clipStopperHidden(ClipStopperType type);
	void notify_automaticLoadSettingsChangedGif();
	void notify_handlePendingHistoryUpdate();

	void cmd_search();
	void cmd_next_chat();
	void cmd_previous_chat();

	~HistoryWidget();

signals:

	void cancelled();
	void historyShown(History *history, MsgId atMsgId);

public slots:

	void onCancel();
	void onReplyToMessage();
	void onEditMessage();
	void onPinMessage();
	void onUnpinMessage();
	void onUnpinMessageSure();
	void onPinnedHide();
	void onCopyPostLink();
	void onFieldBarCancel();

	void onCancelSendAction();

	void onStickerPackInfo();

	void onPreviewParse();
	void onPreviewCheck();
	void onPreviewTimeout();

	void peerUpdated(PeerData *data);
	void onFullPeerUpdated(PeerData *data);

	void onPhotoUploaded(const FullMsgId &msgId, bool silent, const MTPInputFile &file);
	void onDocumentUploaded(const FullMsgId &msgId, bool silent, const MTPInputFile &file);
	void onThumbDocumentUploaded(const FullMsgId &msgId, bool silent, const MTPInputFile &file, const MTPInputFile &thumb);

	void onPhotoProgress(const FullMsgId &msgId);
	void onDocumentProgress(const FullMsgId &msgId);

	void onPhotoFailed(const FullMsgId &msgId);
	void onDocumentFailed(const FullMsgId &msgId);

	void onReportSpamClicked();
	void onReportSpamSure();
	void onReportSpamHide();
	void onReportSpamClear();

	void onScroll();
	void onHistoryToEnd();
	void onCollapseComments();
	void onSend(bool ctrlShiftEnter = false, MsgId replyTo = -1);

	void onUnblock();
	void onBotStart();
	void onJoinChannel();
	void onMuteUnmute();
	void onBroadcastSilentChange();

	void onPhotoSelect();
	void onDocumentSelect();
	void onPhotoDrop(const QMimeData *data);
	void onDocumentDrop(const QMimeData *data);
	void onFilesDrop(const QMimeData *data);

	void onKbToggle(bool manual = true);
	void onCmdStart();

	void activate();
	void onStickersUpdated();
	void onMentionHashtagOrBotCommandInsert(QString str);
	void onTextChange();

	void onFieldTabbed();
	void onStickerSend(DocumentData *sticker);
	void onPhotoSend(PhotoData *photo);
	void onInlineResultSend(InlineResult *result, UserData *bot);

	void onWindowVisibleChanged();

	void deleteMessage();
	void forwardMessage();
	void selectMessage();

	void onForwardHere(); // instead of a reply

	void onFieldFocused();
	void onFieldResize();
	void onCheckMentionDropdown();
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
	void updateField();

	void onRecordError();
	void onRecordDone(QByteArray result, VoiceWaveform waveform, qint32 samples);
	void onRecordUpdate(quint16 level, qint32 samples);

	void onUpdateHistoryItems();

	// checks if we are too close to the top or to the bottom
	// in the scroll area and preloads history if needed
	void preloadHistoryIfNeeded();

private:

	MsgId _replyToId;
	Text _replyToName;
	int32 _replyToNameVersion;
	void updateReplyToName();

	MsgId _editMsgId;

	HistoryItem *_replyEditMsg;
	Text _replyEditMsgText;

	IconedButton _fieldBarCancel;
	void updateReplyEditTexts(bool force = false);

	struct PinnedBar {
		PinnedBar(MsgId msgId, HistoryWidget *parent);

		MsgId msgId;
		HistoryItem *msg;
		Text text;
		IconedButton cancel;
		PlainShadow shadow;
	};
	PinnedBar *_pinnedBar;
	void updatePinnedBar(bool force = false);
	bool pinnedMsgVisibilityUpdated();
	void destroyPinnedBar();
	void unpinDone(const MTPUpdates &updates);

	class ReplyEditMessageDataCallback : public SharedCallback2<void, ChannelData*, MsgId> {
	public:
		void call(ChannelData *channel, MsgId msgId) const override;
	};

	void sendExistingDocument(DocumentData *doc, const QString &caption);
	void sendExistingPhoto(PhotoData *photo, const QString &caption);

	void drawField(Painter &p);
	void drawRecordButton(Painter &p);
	void drawRecording(Painter &p);
	void drawPinnedBar(Painter &p);

	void updateMouseTracking();

	// destroys _history and _migrated unread bars
	void destroyUnreadBar();

	mtpRequestId _saveEditMsgRequestId;
	void saveEditMsg();
	void saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req);
	bool saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req);

	DBIPeerReportSpamStatus _reportSpamStatus;
	mtpRequestId _reportSpamSettingRequestId;
	static const mtpRequestId ReportSpamRequestNeeded = -1;
	void updateReportSpamStatus();
	void requestReportSpamSetting();
	void reportSpamSettingDone(const MTPPeerSettings &result, mtpRequestId req);
	bool reportSpamSettingFail(const RPCError &error, mtpRequestId req);

	QString _previewLinks;
	WebPageData *_previewData;
	typedef QMap<QString, WebPageId> PreviewCache;
	PreviewCache _previewCache;
	mtpRequestId _previewRequest;
	Text _previewTitle, _previewDescription;
	SingleTimer _previewTimer;
	bool _previewCancelled;
	void gotPreview(QString links, const MTPMessageMedia &media, mtpRequestId req);

	bool _replyForwardPressed;

	HistoryItem *_replyReturn;
	QList<MsgId> _replyReturns;

	bool messagesFailed(const RPCError &error, mtpRequestId requestId);
	void addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed);
	void addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed);

	enum ScrollChangeType {
		ScrollChangeNone,
		ScrollChangeAdd,
		ScrollChangeOldHistoryHeight,
	};
	struct ScrollChange {
		ScrollChangeType type;
		int value;
	};
	void updateListSize(bool initial = false, bool loadedDown = false, const ScrollChange &change = { ScrollChangeNone, 0 });

	void saveGifDone(DocumentData *doc, const MTPBool &result);

	void reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId request);
	bool reportSpamFail(const RPCError &error, mtpRequestId request);

	void unblockDone(PeerData *peer, const MTPBool &result, mtpRequestId req);
	bool unblockFail(const RPCError &error, mtpRequestId req);
	void blockDone(PeerData *peer, const MTPBool &result);

	void joinDone(const MTPUpdates &result, mtpRequestId req);
	bool joinFail(const RPCError &error, mtpRequestId req);

	void countHistoryShowFrom();

	mtpRequestId _stickersUpdateRequest;
	void stickersGot(const MTPmessages_AllStickers &stickers);
	bool stickersFailed(const RPCError &error);

	mtpRequestId _savedGifsUpdateRequest;
	void savedGifsGot(const MTPmessages_SavedGifs &gifs);
	bool savedGifsFailed(const RPCError &error);

	void writeDrafts(HistoryDraft **msgDraft, HistoryEditDraft **editDraft);
	void writeDrafts(History *history);
	void setFieldText(const QString &text, int32 textUpdateEventsFlags = 0, bool clearUndoHistory = true);

	QStringList getMediasFromMime(const QMimeData *d);

	void updateDragAreas();

	bool readyToForward() const;
	bool hasBroadcastToggle() const;
	bool hasSilentToggle() const;

	PeerData *_peer, *_clearPeer; // cache _peer in _clearPeer when showing clear history box
	ChannelId _channel;
	bool _canSendMessages;
	MsgId _showAtMsgId, _fixedInScrollMsgId;
	int32 _fixedInScrollMsgTop;

	mtpRequestId _firstLoadRequest, _preloadRequest, _preloadDownRequest;

	MsgId _delayedShowAtMsgId;
	mtpRequestId _delayedShowAtRequest;

	MsgId _activeAnimMsgId;

	ScrollArea _scroll;
	HistoryInner *_list;
	History *_migrated, *_history;
	bool _histInited; // initial updateListSize() called

	int32 _lastScroll;
	uint64 _lastScrolled;
	QTimer _updateHistoryItems; // gifs optimization

	IconedButton _toHistoryEnd;
	CollapseButton _collapseComments;

	MentionsDropdown _attachMention;
	UserData *_inlineBot;
	QString _inlineBotUsername;
	mtpRequestId _inlineBotResolveRequestId;
	void inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result);
	bool inlineBotResolveFail(QString name, const RPCError &error);

	bool isBotStart() const;
	bool isBlocked() const;
	bool isJoinChannel() const;
	bool isMuteUnmute() const;
	bool updateCmdStartShown();

	ReportSpamPanel _reportSpamPanel;

	FlatButton _send, _unblock, _botStart, _joinChannel, _muteUnmute;
	mtpRequestId _unblockRequest, _reportSpamRequest;
	IconedButton _attachDocument, _attachPhoto;
	EmojiButton _attachEmoji;
	IconedButton _kbShow, _kbHide, _cmdStart;
	FlatCheckbox _broadcast;
	SilentToggle _silent;
	bool _cmdStartShown;
	MessageField _field;
	Animation _a_record, _a_recording;
	bool _recording, _inRecord, _inField, _inReplyEdit, _inPinnedMsg;
	anim::ivalue a_recordingLevel;
	int32 _recordingSamples;
	anim::fvalue a_recordOver, a_recordDown;
	anim::cvalue a_recordCancel;
	int32 _recordCancelWidth;

	bool kbWasHidden() const;

	bool _kbShown;
	HistoryItem *_kbReplyTo;
	ScrollArea _kbScroll;
	BotKeyboard _keyboard;

	Dropdown _attachType;
	EmojiPan _emojiPan;
	DragState _attachDrag;
	DragArea _attachDragDocument, _attachDragPhoto;

	int32 _selCount; // < 0 - text selected, focus list, not _field

	TaskQueue _fileLoader;
	int32 _textUpdateEventsFlags;

	int64 _serviceImageCacheSize;
	QString _confirmSource;

	uint64 _confirmWithTextId;

	QString _titlePeerText;
	int32 _titlePeerTextWidth;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver, _cacheTopBarUnder, _cacheTopBarOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	QTimer _scrollTimer;
	int32 _scrollDelta;

	QTimer _animActiveTimer;
	float64 _animActiveStart;

	QMap<QPair<History*, SendActionType>, mtpRequestId> _sendActionRequests;
	QTimer _sendActionStopTimer;

	uint64 _saveDraftStart;
	bool _saveDraftText;
	QTimer _saveDraftTimer;

	PlainShadow _sideShadow, _topShadow;
	bool _inGrab;

};

