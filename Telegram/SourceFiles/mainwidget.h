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

#include "storage/localimageloader.h"
#include "history/history_common.h"
#include "core/single_timer.h"
#include "base/weak_unique_ptr.h"

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Dialogs {
class Row;
} // namespace Dialogs

namespace Media {
namespace Player {
class Widget;
class VolumeWidget;
class Panel;
class Float;
} // namespace Player
} // namespace Media

namespace Ui {
class PlainShadow;
class DropdownMenu;
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui

namespace Window {
class Controller;
class PlayerWrapWidget;
class TopBarWidget;
class SectionMemento;
class SectionWidget;
class AbstractSectionWidget;
struct SectionSlideParams;
enum class Column;
} // namespace Window

namespace Calls {
class Call;
class TopBar;
} // namespace Calls

class MainWindow;
class ConfirmBox;
class DialogsWidget;
class HistoryWidget;
class OverviewWidget;
class HistoryHider;

enum StackItemType {
	HistoryStackItem,
	SectionStackItem,
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
	StackItemHistory(PeerData *peer, MsgId msgId, QList<MsgId> replyReturns) : StackItem(peer)
		, msgId(msgId)
		, replyReturns(replyReturns) {
	}
	StackItemType type() const {
		return HistoryStackItem;
	}
	MsgId msgId;
	QList<MsgId> replyReturns;
};

class StackItemSection : public StackItem {
public:
	StackItemSection(std::unique_ptr<Window::SectionMemento> &&memento);
	~StackItemSection();

	StackItemType type() const {
		return SectionStackItem;
	}
	Window::SectionMemento *memento() const {
		return _memento.get();
	}

private:
	std::unique_ptr<Window::SectionMemento> _memento;

};

class StackItemOverview : public StackItem {
public:
	StackItemOverview(PeerData *peer, MediaOverviewType mediaType, int32 lastWidth, int32 lastScrollTop) : StackItem(peer)
		, mediaType(mediaType)
		, lastWidth(lastWidth)
		, lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return OverviewStackItem;
	}
	MediaOverviewType mediaType;
	int32 lastWidth, lastScrollTop;
};

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

class MainWidget : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	MainWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller);

	bool isSectionShown() const;

	// Temporary methods, while top bar was not done inside HistoryWidget / OverviewWidget.
	bool paintTopBar(Painter &, int decreaseWidth, TimeMs ms);
	QRect getMembersShowAreaGeometry() const;
	void setMembersShowAreaActive(bool active);

	int contentScrollAddToY() const;

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

	void start(const MTPUser *self = nullptr);

	void openPeerByName(const QString &name, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const MTPInputStickerSet &set);

	bool started();
	void applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history = 0);

	void updateNotifySetting(PeerData *peer, NotifySettingStatus notify, SilentNotifiesStatus silent = SilentNotifiesDontChange);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialog(History *history);
	void removeDialog(History *history);
	void dlgUpdated();
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(PeerData *peer, MsgId msgId);

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
	void markActiveHistoryAsRead();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	PeerData *historyPeer();
	PeerData *peer();

	PeerData *activePeer();
	MsgId activeMsgId();

	int backgroundFromY() const;
	PeerData *overviewPeer();
	bool showMediaTypeSwitch() const;
	void showWideSection(Window::SectionMemento &&memento);
	void showMediaOverview(PeerData *peer, MediaOverviewType type, bool back = false, int32 lastScrollTop = -1);
	bool stackIsEmpty() const;
	void showBackFromStack();
	void orderWidgets();
	QRect historyRect() const;
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);

	void onSendFileConfirm(const FileLoadResultPtr &file);
	bool onSendSticker(DocumentData *sticker);

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	bool isActive() const;
	bool doWeReadServerHistory() const;
	bool lastWasOnline() const;
	TimeMs lastSetOnline() const;

	void saveDraftToCloud();
	void applyCloudDraft(History *history);
	void writeDrafts(History *history);

	int32 dlgsWidth() const;

	void showForwardLayer(const SelectedItemSet &items);
	void showSendPathsLayer();
	void deleteLayer(int selectedCount = 0); // 0 - context item
	void cancelUploadLayer();
	void shareContactLayer(UserData *contact);
	void shareUrlLayer(const QString &url, const QString &text);
	void inlineSwitchLayer(const QString &botAndQuery);
	void hiderLayer(object_ptr<HistoryHider> h);
	void noHider(HistoryHider *destroyed);
	bool setForwardDraft(PeerId peer, ForwardWhatMessages what);
	bool setForwardDraft(PeerId peer, const SelectedItemSet &items);
	bool onShareUrl(const PeerId &peer, const QString &url, const QString &text);
	bool onInlineSwitchChosen(const PeerId &peer, const QString &botAndQuery);
	void onShareContact(const PeerId &peer, UserData *contact);
	bool onSendPaths(const PeerId &peer);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer(bool withConfirm = false) const;
	bool selectingPeerForInlineSwitch();
	void offerPeer(PeerId peer);
	void dialogsActivate();

	void deletePhotoLayer(PhotoData *photo);

	DragState getDragState(const QMimeData *mime);

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates);
	void deleteMessages(PeerData *peer, const QVector<MTPint> &ids, bool forEveryone);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteConversation(PeerData *peer, bool deleteHistory = true);
	void deleteAndExit(ChatData *chat);
	void clearHistory(PeerData *peer);
	void deleteAllFromUser(ChannelData *channel, UserData *from);

	void addParticipants(PeerData *chatOrChannel, const std::vector<gsl::not_null<UserData*>> &users);
	struct UserAndPeer {
		UserData *user;
		PeerData *peer;
	};
	bool addParticipantFail(UserAndPeer data, const RPCError &e);
	bool addParticipantsFail(ChannelData *channel, const RPCError &e); // for multi invite in channels

	void kickParticipant(ChatData *chat, UserData *user);
	bool kickParticipantFail(ChatData *chat, const RPCError &e);

	void checkPeerHistory(PeerData *peer);
	void checkedHistory(PeerData *peer, const MTPmessages_Messages &result);

	bool sendMessageFail(const RPCError &error);

	void forwardSelectedItems();
	void confirmDeleteSelectedItems();
	void clearSelectedItems();

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	Dialogs::IndexedList *contactsNoDialogsList();

	struct MessageToSend {
		History *history = nullptr;
		TextWithTags textWithTags;
		MsgId replyTo = 0;
		bool silent = false;
		WebPageId webPageId = 0;
		bool clearDraft = true;
	};
	void sendMessage(const MessageToSend &message);
	void saveRecentHashtags(const QString &text);

    void readServerHistory(History *history, ReadServerHistoryChecks checks = ReadServerHistoryChecks::OnlyIfUnread);
	void unreadCountChanged(History *history);

	TimeMs animActiveTimeStart(const HistoryItem *msg) const;
	void stopAnimActive();

	void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	void jumpToDate(gsl::not_null<PeerData*> peer, const QDate &date);
	void searchMessages(const QString &query, PeerData *inPeer);
	bool preloadOverview(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemEdited(HistoryItem *item);

	void loadMediaBack(PeerData *peer, MediaOverviewType type, bool many = false);

	void checkLastUpdate(bool afterSleep);

	void insertCheckedServiceNotification(const TextWithEntities &message, const MTPMessageMedia &media, int32 date);
	void serviceHistoryDone(const MTPmessages_Messages &msgs);
	bool serviceHistoryFail(const RPCError &error);

	bool isIdle() const;

	QPixmap cachedBackground(const QRect &forRect, int &x, int &y);
	void updateScrollColors();

	void setChatBackground(const App::WallPaper &wp);
	bool chatBackgroundLoading();
	float64 chatBackgroundProgress() const;
	void checkChatBackground();
	ImagePtr newBackgroundThumb();

	void messageDataReceived(ChannelData *channel, MsgId msgId);
	void updateBotKeyboard(History *h);

	void pushReplyReturn(HistoryItem *item);

	void cancelForwarding(History *history);
	void finishForwarding(History *history, bool silent); // send them

	void mediaMarkRead(DocumentData *data);
	void mediaMarkRead(const HistoryItemsMap &items);

	void webPageUpdated(WebPageData *page);
	void gameUpdated(GameData *game);
	void updateMutedIn(int32 seconds);

	void updateStickers();

	void choosePeer(PeerId peerId, MsgId showAtMsgId); // does offerPeer or showPeerHistory
	void clearBotStartToken(PeerData *peer);

	void ptsWaiterStartTimerFor(ChannelData *channel, int32 ms); // ms <= 0 - stop timer
	void feedUpdates(const MTPUpdates &updates, uint64 randomId = 0);
	void feedUpdate(const MTPUpdate &update);

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	void fillPeerMenu(PeerData *peer, base::lambda<QAction*(const QString &text, base::lambda<void()> handler)> callback, bool pinToggle);

	void gotRangeDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff);
	void onSelfParticipantUpdated(ChannelData *channel);

	// Mayde public for ApiWrap, while it is still here.
	// Better would be for this to be moved to ApiWrap.
	bool requestingDifference() const {
		return _ptsWaiter.requesting();
	}

	bool contentOverlapped(const QRect &globalRect);

	void documentLoadProgress(DocumentData *document);

	void app_sendBotCallback(const HistoryMessageReplyMarkup::Button *button, const HistoryItem *msg, int row, int col);

	void ui_repaintHistoryItem(gsl::not_null<const HistoryItem*> item);
	void ui_showPeerHistory(quint64 peer, qint32 msgId, Ui::ShowWay way);
	PeerData *ui_getPeerForMouseAction();

	void notify_botCommandsChanged(UserData *bot);
	void notify_inlineBotRequesting(bool requesting);
	void notify_replyMarkupUpdated(const HistoryItem *item);
	void notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);
	void notify_userIsBotChanged(UserData *bot);
	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_migrateUpdated(PeerData *peer);
	void notify_historyItemLayoutChanged(const HistoryItem *item);
	void notify_historyMuteUpdated(History *history);

	bool cmd_search();
	bool cmd_next_chat();
	bool cmd_previous_chat();

	~MainWidget();

signals:
	void peerUpdated(PeerData *peer);
	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void peerPhotoChanged(PeerData *peer);
	void dialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);
	void dialogsUpdated();
	void stickersUpdated();

public slots:
	void webPagesOrGamesUpdate();

	void documentLoadProgress(FileLoader *loader);
	void documentLoadFailed(FileLoader *loader, bool started);
	void inlineResultLoadProgress(FileLoader *loader);
	void inlineResultLoadFailed(FileLoader *loader, bool started);

	void dialogsCancelled();

	void getDifference();
	void onGetDifferenceTimeByPts();
	void onGetDifferenceTimeAfterFail();
	void mtpPing();

	void updateOnline(bool gotOtherOffline = false);
	void checkIdleFinish();
	void updateOnlineDisplay();

	void onHistoryShown(History *history, MsgId atMsgId);

	void searchInPeer(PeerData *peer);

	void onUpdateNotifySettings();

	void onCacheBackground();

	void onInviteImport();

	void onUpdateMuted();

	void onStickersInstalled(uint64 setId);

	void onViewsIncrement();

	void ui_showPeerHistoryAsync(quint64 peerId, qint32 showAtMsgId, Ui::ShowWay way);
	void ui_autoplayMediaInlineAsync(qint32 channelId, qint32 msgId);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	struct Float {
		template <typename ToggleCallback, typename DraggedCallback>
		Float(QWidget *parent, HistoryItem *item, ToggleCallback callback, DraggedCallback dragged);

		bool hiddenByWidget = false;
		bool hiddenByHistory = false;
		bool visible = false;
		RectPart animationSide;
		Animation visibleAnimation;
		Window::Column column;
		RectPart corner;
		QPoint dragFrom;
		Animation draggedAnimation;
		bool hiddenByDrag = false;
		object_ptr<Media::Player::Float> widget;
	};

	using ChannelGetDifferenceTime = QMap<ChannelData*, TimeMs>;
	enum class ChannelDifferenceRequest {
		Unknown,
		PtsGapOrShortPoll,
		AfterFail,
	};

	struct DeleteHistoryRequest {
		PeerData *peer;
		bool justClearHistory;
	};

	struct DeleteAllFromUserParams {
		ChannelData *channel;
		UserData *from;
	};

	void animationCallback();
	void handleAdaptiveLayoutUpdate();
	void updateWindowAdaptiveLayout();
	void handleAudioUpdate(const AudioMsgId &audioId);
	void updateMediaPlayerPosition();
	void updateMediaPlaylistPosition(int x);
	void updateControlsGeometry();
	void updateDialogsWidthAnimated();

	void createPlayer();
	void switchToPanelPlayer();
	void switchToFixedPlayer();
	void closeBothPlayers();
	void playerHeightUpdated();

	void setCurrentCall(Calls::Call *call);
	void createCallTopBar();
	void destroyCallTopBar();
	void callTopBarHeightUpdated();

	void sendReadRequest(PeerData *peer, MsgId upTo);
	void channelReadDone(PeerData *peer, const MTPBool &result);
	void historyReadDone(PeerData *peer, const MTPmessages_AffectedMessages &result);
	bool readRequestFail(PeerData *peer, const RPCError &error);
	void readRequestDone(PeerData *peer);

	void messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result);
	void overviewLoaded(History *history, const MTPmessages_Messages &result, mtpRequestId req);
	void mediaOverviewUpdated(const Notify::PeerUpdate &update);

	Window::SectionSlideParams prepareShowAnimation(bool willHaveTopBarShadow, bool willHaveTabbedSection);
	void showNewWideSection(Window::SectionMemento &&memento, bool back, bool saveInStack);

	// All this methods use the prepareShowAnimation().
	Window::SectionSlideParams prepareWideSectionAnimation(Window::SectionWidget *section);
	Window::SectionSlideParams prepareHistoryAnimation(PeerId historyPeerId);
	Window::SectionSlideParams prepareOverviewAnimation();
	Window::SectionSlideParams prepareDialogsAnimation();

	void startWithSelf(const MTPUserFull &user);

	void saveSectionInStack();

	void getChannelDifference(ChannelData *channel, ChannelDifferenceRequest from = ChannelDifferenceRequest::Unknown);
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

	void deleteHistoryPart(DeleteHistoryRequest request, const MTPmessages_AffectedHistory &result);
	void deleteAllFromUserPart(DeleteAllFromUserParams params, const MTPmessages_AffectedHistory &result);

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result);
	bool usernameResolveFail(QString name, const RPCError &error);

	void inviteCheckDone(QString hash, const MTPChatInvite &invite);
	bool inviteCheckFail(const RPCError &error);
	void inviteImportDone(const MTPUpdates &result);
	bool inviteImportFail(const RPCError &error);

	int getSectionTop() const;

	void hideAll();
	void showAll();

	void overviewPreloaded(PeerData *data, const MTPmessages_Messages &result, mtpRequestId req);
	bool overviewFailed(PeerData *data, const RPCError &error, mtpRequestId req);

	void clearCachedBackground();
	void checkCurrentFloatPlayer();
	void toggleFloatPlayer(gsl::not_null<Float*> instance);
	void checkFloatPlayerVisibility();
	void updateFloatPlayerPosition(gsl::not_null<Float*> instance);
	void removeFloatPlayer(gsl::not_null<Float*> instance);
	Float *currentFloatPlayer() const {
		return _playerFloats.empty() ? nullptr : _playerFloats.back().get();
	}
	Window::AbstractSectionWidget *getFloatPlayerSection(gsl::not_null<Window::Column*> column) const;
	void finishFloatPlayerDrag(gsl::not_null<Float*> instance, bool closed);
	void updateFloatPlayerColumnCorner(QPoint center);
	QPoint getFloatPlayerPosition(gsl::not_null<Float*> instance) const;
	QPoint getFloatPlayerHiddenPosition(QPoint position, QSize size, RectPart side) const;
	RectPart getFloatPlayerSide(QPoint center) const;

	bool ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdate &update);
	bool ptsUpdateAndApply(int32 pts, int32 ptsCount);
	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, TimeMs &curTime);

	void viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req);
	bool viewsIncrementFail(const RPCError &error, mtpRequestId req);

	gsl::not_null<Window::Controller*> _controller;
	bool _started = false;

	OrderedSet<WebPageId> _webPagesUpdated;
	OrderedSet<GameId> _gamesUpdated;
	QTimer _webPageOrGameUpdater;

	SingleTimer _updateMutedTimer;

	QString _inviteHash;

	Animation _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

	int _dialogsWidth;
	Animation _a_dialogsWidth;

	object_ptr<Ui::PlainShadow> _sideShadow;
	object_ptr<TWidget> _sideResizeArea;
	object_ptr<DialogsWidget> _dialogs;
	object_ptr<HistoryWidget> _history;
	object_ptr<Window::SectionWidget> _wideSection = { nullptr };
	object_ptr<Window::SectionWidget> _thirdSection = { nullptr };
	object_ptr<OverviewWidget> _overview = { nullptr };

	base::weak_unique_ptr<Calls::Call> _currentCall;
	object_ptr<Ui::WidgetSlideWrap<Calls::TopBar>> _callTopBar = { nullptr };

	object_ptr<Window::PlayerWrapWidget> _player = { nullptr };
	object_ptr<Media::Player::VolumeWidget> _playerVolume = { nullptr };
	object_ptr<Media::Player::Panel> _playerPlaylist;
	object_ptr<Media::Player::Panel> _playerPanel;
	bool _playerUsingPanel = false;
	std::vector<std::unique_ptr<Float>> _playerFloats;

	QPointer<ConfirmBox> _forwardConfirm; // for single column layout
	object_ptr<HistoryHider> _hider = { nullptr };
	std::vector<std::unique_ptr<StackItem>> _stack;
	PeerData *_peerInStack = nullptr;
	MsgId _msgIdInStack = 0;

	int _playerHeight = 0;
	int _callTopBarHeight = 0;
	int _contentScrollAddToY = 0;

	int32 updDate = 0;
	int32 updQts = -1;
	int32 updSeq = 0;
	SingleTimer noUpdatesTimer;

	PtsWaiter _ptsWaiter;

	ChannelGetDifferenceTime _channelGetDifferenceTimeByPts, _channelGetDifferenceTimeAfterFail;
	TimeMs _getDifferenceTimeByPts = 0;
	TimeMs _getDifferenceTimeAfterFail = 0;

	SingleTimer _byPtsTimer;

	QMap<int32, MTPUpdates> _bySeqUpdates;
	SingleTimer _bySeqTimer;

	SingleTimer _byMinChannelTimer;

	mtpRequestId _onlineRequest = 0;
	SingleTimer _onlineTimer, _onlineUpdater, _idleFinishTimer;
	bool _lastWasOnline = false;
	TimeMs _lastSetOnline = 0;
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

	TimeMs _lastUpdateTime = 0;
	bool _handlingChannelDifference = false;

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX = 0;
	int _cachedY = 0;
	SingleTimer _cacheBackgroundTimer;

	typedef QMap<ChannelData*, bool> UpdatedChannels;
	UpdatedChannels _updatedChannels;

	PhotoData *_deletingPhoto = nullptr;

	typedef QMap<MsgId, bool> ViewsIncrementMap;
	typedef QMap<PeerData*, ViewsIncrementMap> ViewsIncrement;
	ViewsIncrement _viewsIncremented, _viewsToIncrement;
	typedef QMap<PeerData*, mtpRequestId> ViewsIncrementRequests;
	ViewsIncrementRequests _viewsIncrementRequests;
	typedef QMap<mtpRequestId, PeerData*> ViewsIncrementByRequest;
	ViewsIncrementByRequest _viewsIncrementByRequest;
	SingleTimer _viewsIncrementTimer;

	std::unique_ptr<App::WallPaper> _background;

	bool _resizingSide = false;
	int _resizingSideShift = 0;

};
