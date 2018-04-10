/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/single_timer.h"
#include "base/weak_ptr.h"
#include "ui/rp_widget.h"

struct HistoryMessageMarkupButton;
class MainWindow;
class ConfirmBox;
class DialogsWidget;
class HistoryWidget;
class HistoryHider;
class StackItem;
struct FileLoadResult;

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Dialogs {
struct RowDescriptor;
class Row;
class Key;
class IndexedList;
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
class ResizeArea;
class PlainShadow;
class DropdownMenu;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class Controller;
class PlayerWrapWidget;
class SectionMemento;
class SectionWidget;
class AbstractSectionWidget;
struct SectionSlideParams;
struct SectionShow;
enum class Column;
} // namespace Window

namespace Calls {
class Call;
class TopBar;
} // namespace Calls

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace InlineBots

class MainWidget : public Ui::RpWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	using SectionShow = Window::SectionShow;

	MainWidget(QWidget *parent, not_null<Window::Controller*> controller);

	bool isMainSectionShown() const;
	bool isThirdSectionShown() const;

	int contentScrollAddToY() const;

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

	void start(const MTPUser *self = nullptr);

	void openPeerByName(const QString &name, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const MTPInputStickerSet &set);

	bool started();
	void applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings,
		History *history = 0);

	void updateNotifySettings(
		not_null<PeerData*> peer,
		Data::NotifySettings::MuteChange mute,
		Data::NotifySettings::SilentPostsChange silent
			= Data::NotifySettings::SilentPostsChange::Ignore,
		int muteForSeconds = 86400 * 365);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialog(Dialogs::Key key);
	void removeDialog(Dialogs::Key key);
	void repaintDialogRow(Dialogs::Mode list, not_null<Dialogs::Row*> row);
	void repaintDialogRow(not_null<History*> history, MsgId messageId);

	void windowShown();

	void sentUpdatesReceived(uint64 randomId, const MTPUpdates &updates);
	void sentUpdatesReceived(const MTPUpdates &updates) {
		return sentUpdatesReceived(0, updates);
	}
	bool deleteChannelFailed(const RPCError &error);
	void inviteToChannelDone(
		not_null<ChannelData*> channel,
		const MTPUpdates &updates);
	void historyToDown(History *hist);
	void dialogsToUp();
	void newUnreadMsg(
		not_null<History*> history,
		not_null<HistoryItem*> item);
	void markActiveHistoryAsRead();

	Dialogs::RowDescriptor chatListEntryBefore(
		const Dialogs::RowDescriptor &which) const;
	Dialogs::RowDescriptor chatListEntryAfter(
		const Dialogs::RowDescriptor &which) const;

	PeerData *peer();

	int backgroundFromY() const;
	void showSection(
		Window::SectionMemento &&memento,
		const SectionShow &params);
	void updateColumnLayout();
	bool stackIsEmpty() const;
	void showBackFromStack(
		const SectionShow &params);
	void orderWidgets();
	QRect historyRect() const;
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);
	void checkMainSectionToLayer();

	void onSendFileConfirm(const std::shared_ptr<FileLoadResult> &file);
	bool onSendSticker(DocumentData *sticker);

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	bool isActive() const;
	bool doWeReadServerHistory() const;
	bool doWeReadMentions() const;
	bool lastWasOnline() const;
	TimeMs lastSetOnline() const;

	void saveDraftToCloud();
	void applyCloudDraft(History *history);
	void writeDrafts(History *history);

	int32 dlgsWidth() const;

	void showForwardLayer(MessageIdsList &&items);
	void showSendPathsLayer();
	void deleteLayer(FullMsgId itemId);
	void cancelUploadLayer(not_null<HistoryItem*> item);
	void shareUrlLayer(const QString &url, const QString &text);
	void inlineSwitchLayer(const QString &botAndQuery);
	void hiderLayer(object_ptr<HistoryHider> h);
	void noHider(HistoryHider *destroyed);
	bool setForwardDraft(PeerId peer, MessageIdsList &&items);
	bool shareUrl(
		not_null<PeerData*> peer,
		const QString &url,
		const QString &text);
	void replyToItem(not_null<HistoryItem*> item);
	bool onInlineSwitchChosen(const PeerId &peer, const QString &botAndQuery);
	bool onSendPaths(const PeerId &peer);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer(bool withConfirm = false) const;
	bool selectingPeerForInlineSwitch();
	void offerPeer(PeerId peer);
	void dialogsActivate();

	void deletePhotoLayer(PhotoData *photo);

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates);
	void deleteMessages(
		not_null<PeerData*> peer,
		const QVector<MTPint> &ids,
		bool forEveryone);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteConversation(
		not_null<PeerData*> peer,
		bool deleteHistory = true);
	void deleteAndExit(ChatData *chat);
	void deleteAllFromUser(ChannelData *channel, UserData *from);

	void addParticipants(
		not_null<PeerData*> chatOrChannel,
		const std::vector<not_null<UserData*>> &users);
	struct UserAndPeer {
		UserData *user;
		PeerData *peer;
	};
	bool addParticipantFail(UserAndPeer data, const RPCError &e);
	bool addParticipantsFail(
		not_null<ChannelData*> channel,
		const RPCError &e); // for multi invite in channels

	bool sendMessageFail(const RPCError &error);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	Dialogs::IndexedList *contactsNoDialogsList();

	struct MessageToSend {
		MessageToSend(not_null<History*> history) : history(history) {
		}

		not_null<History*> history;
		TextWithTags textWithTags;
		MsgId replyTo = 0;
		WebPageId webPageId = 0;
		bool clearDraft = true;
	};
	void sendMessage(const MessageToSend &message);
	void saveRecentHashtags(const QString &text);

	void unreadCountChanged(not_null<History*> history);

	// While HistoryInner is not HistoryView::ListWidget.
	TimeMs highlightStartTime(not_null<const HistoryItem*> item) const;
	bool historyInSelectionMode() const;

	void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	void searchMessages(const QString &query, Dialogs::Key inChat);
	void itemEdited(not_null<HistoryItem*> item);

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

	void pushReplyReturn(not_null<HistoryItem*> item);

	void cancelForwarding(not_null<History*> history);
	void finishForwarding(not_null<History*> history);

	void updateMutedIn(TimeMs delay);

	// Does offerPeer or showPeerHistory.
	void choosePeer(PeerId peerId, MsgId showAtMsgId);
	void clearBotStartToken(PeerData *peer);

	void ptsWaiterStartTimerFor(ChannelData *channel, int32 ms); // ms <= 0 - stop timer
	void feedUpdates(const MTPUpdates &updates, uint64 randomId = 0);

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	void onSelfParticipantUpdated(ChannelData *channel);
	void feedChannelDifference(const MTPDupdates_channelDifference &data);

	// Mayde public for ApiWrap, while it is still here.
	// Better would be for this to be moved to ApiWrap.
	bool requestingDifference() const {
		return _ptsWaiter.requesting();
	}

	bool contentOverlapped(const QRect &globalRect);

	bool ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdate &update);
	bool ptsUpdateAndApply(int32 pts, int32 ptsCount);

	void documentLoadProgress(DocumentData *document);

	void searchInChat(Dialogs::Key chat);

	void app_sendBotCallback(
		not_null<const HistoryMessageMarkupButton*> button,
		not_null<const HistoryItem*> msg,
		int row,
		int column);

	void ui_showPeerHistory(
		PeerId peer,
		const SectionShow &params,
		MsgId msgId);
	PeerData *ui_getPeerForMouseAction();

	void notify_botCommandsChanged(UserData *bot);
	void notify_inlineBotRequesting(bool requesting);
	void notify_replyMarkupUpdated(const HistoryItem *item);
	void notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);
	void notify_userIsBotChanged(UserData *bot);
	void notify_migrateUpdated(PeerData *peer);
	void notify_historyMuteUpdated(History *history);

	bool cmd_search();
	bool cmd_next_chat();
	bool cmd_previous_chat();

	~MainWidget();

signals:
	void dialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);
	void dialogsUpdated();

public slots:
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

	void onUpdateNotifySettings();

	void onCacheBackground();

	void onInviteImport();

	void onUpdateMuted();

	void onViewsIncrement();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	struct Float {
		template <typename ToggleCallback, typename DraggedCallback>
		Float(
			QWidget *parent,
			not_null<Window::Controller*> controller,
			not_null<HistoryItem*> item,
			ToggleCallback toggle,
			DraggedCallback dragged);

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
	void updateThirdColumnToCurrentChat(
		Dialogs::Key key,
		bool canWrite);
	[[nodiscard]] bool saveThirdSectionToStackBack() const;
	[[nodiscard]] auto thirdSectionForCurrentMainSection(Dialogs::Key key)
		-> std::unique_ptr<Window::SectionMemento>;
	void userIsContactUpdated(not_null<UserData*> user);

	void createPlayer();
	void switchToPanelPlayer();
	void switchToFixedPlayer();
	void closeBothPlayers();
	void playerHeightUpdated();

	void setCurrentCall(Calls::Call *call);
	void createCallTopBar();
	void destroyCallTopBar();
	void callTopBarHeightUpdated(int callTopBarHeight);

	void messagesAffected(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result);

	Window::SectionSlideParams prepareShowAnimation(
		bool willHaveTopBarShadow);
	void showNewSection(
		Window::SectionMemento &&memento,
		const SectionShow &params);
	void dropMainSection(Window::SectionWidget *widget);

	Window::SectionSlideParams prepareThirdSectionAnimation(Window::SectionWidget *section);

	// All this methods use the prepareShowAnimation().
	Window::SectionSlideParams prepareMainSectionAnimation(Window::SectionWidget *section);
	Window::SectionSlideParams prepareHistoryAnimation(PeerId historyPeerId);
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

	void feedUpdateVector(
		const MTPVector<MTPUpdate> &updates,
		bool skipMessageIds = false);
	// Doesn't call sendHistoryChangeNotifications itself.
	void feedMessageIds(const MTPVector<MTPUpdate> &updates);
	// Doesn't call sendHistoryChangeNotifications itself.
	void feedUpdate(const MTPUpdate &update);

	void deleteHistoryPart(DeleteHistoryRequest request, const MTPmessages_AffectedHistory &result);

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result);
	bool usernameResolveFail(QString name, const RPCError &error);

	void inviteCheckDone(QString hash, const MTPChatInvite &invite);
	bool inviteCheckFail(const RPCError &error);
	void inviteImportDone(const MTPUpdates &result);
	bool inviteImportFail(const RPCError &error);

	int getMainSectionTop() const;
	int getThirdSectionTop() const;

	void hideAll();
	void showAll();

	void clearCachedBackground();
	void checkCurrentFloatPlayer();
	void createFloatPlayer(not_null<HistoryItem*> item);
	void toggleFloatPlayer(not_null<Float*> instance);
	void checkFloatPlayerVisibility();
	void updateFloatPlayerPosition(not_null<Float*> instance);
	void removeFloatPlayer(not_null<Float*> instance);
	Float *currentFloatPlayer() const {
		return _playerFloats.empty() ? nullptr : _playerFloats.back().get();
	}
	Window::AbstractSectionWidget *getFloatPlayerSection(
		Window::Column column) const;
	void finishFloatPlayerDrag(
		not_null<Float*> instance,
		bool closed);
	void updateFloatPlayerColumnCorner(QPoint center);
	QPoint getFloatPlayerPosition(not_null<Float*> instance) const;
	QPoint getFloatPlayerHiddenPosition(
		QPoint position,
		QSize size,
		RectPart side) const;
	RectPart getFloatPlayerSide(QPoint center) const;

	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, TimeMs &curTime);

	void viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req);
	bool viewsIncrementFail(const RPCError &error, mtpRequestId req);

	void refreshResizeAreas();
	template <typename MoveCallback, typename FinishCallback>
	void createResizeArea(
		object_ptr<Ui::ResizeArea> &area,
		MoveCallback &&moveCallback,
		FinishCallback &&finishCallback);
	void ensureFirstColumnResizeAreaCreated();
	void ensureThirdColumnResizeAreaCreated();

	void updateNotifySettingsLocal(
		not_null<PeerData*> peer,
		History *history = nullptr);

	not_null<Window::Controller*> _controller;
	bool _started = false;

	SingleTimer _updateMutedTimer;

	QString _inviteHash;

	Animation _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

	int _dialogsWidth = 0;
	int _thirdColumnWidth = 0;
	Animation _a_dialogsWidth;

	object_ptr<Ui::PlainShadow> _sideShadow;
	object_ptr<Ui::PlainShadow> _thirdShadow = { nullptr };
	object_ptr<Ui::ResizeArea> _firstColumnResizeArea = { nullptr };
	object_ptr<Ui::ResizeArea> _thirdColumnResizeArea = { nullptr };
	object_ptr<DialogsWidget> _dialogs;
	object_ptr<HistoryWidget> _history;
	object_ptr<Window::SectionWidget> _mainSection = { nullptr };
	object_ptr<Window::SectionWidget> _thirdSection = { nullptr };
	std::unique_ptr<Window::SectionMemento> _thirdSectionFromStack;

	base::weak_ptr<Calls::Call> _currentCall;
	object_ptr<Ui::SlideWrap<Calls::TopBar>> _callTopBar = { nullptr };

	object_ptr<Window::PlayerWrapWidget> _player = { nullptr };
	object_ptr<Media::Player::VolumeWidget> _playerVolume = { nullptr };
	object_ptr<Media::Player::Panel> _playerPlaylist;
	object_ptr<Media::Player::Panel> _playerPanel;
	bool _playerUsingPanel = false;
	std::vector<std::unique_ptr<Float>> _playerFloats;

	QPointer<ConfirmBox> _forwardConfirm; // for single column layout
	object_ptr<HistoryHider> _hider = { nullptr };
	std::vector<std::unique_ptr<StackItem>> _stack;

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
	SingleTimer _onlineTimer, _idleFinishTimer;
	bool _lastWasOnline = false;
	TimeMs _lastSetOnline = 0;
	bool _isIdle = false;

	base::flat_set<not_null<PeerData*>> updateNotifySettingPeers;
	SingleTimer updateNotifySettingTimer;

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

	bool _firstColumnResizing = false;
	int _firstColumnResizingShift = 0;

};
