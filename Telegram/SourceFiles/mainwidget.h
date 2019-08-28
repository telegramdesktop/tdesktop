/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "media/player/media_player_float.h"
#include "data/data_pts_waiter.h"

struct HistoryMessageMarkupButton;
class MainWindow;
class ConfirmBox;
class HistoryWidget;
class StackItem;
struct FileLoadResult;

namespace Api {
struct SendAction;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Data {
class WallPaper;
} // namespace Data

namespace Dialogs {
struct RowDescriptor;
class Row;
class Key;
class Widget;
} // namespace Dialogs

namespace Media {
namespace Player {
class Widget;
class VolumeWidget;
class Panel;
struct TrackState;
} // namespace Player
} // namespace Media

namespace Export {
namespace View {
class TopBar;
class PanelController;
struct Content;
} // namespace View
} // namespace Export

namespace Ui {
class ResizeArea;
class PlainShadow;
class DropdownMenu;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
template <typename Inner>
class TopBarWrapWidget;
class SectionMemento;
class SectionWidget;
class AbstractSectionWidget;
class ConnectionState;
struct SectionSlideParams;
struct SectionShow;
enum class Column;
class HistoryHider;
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

class MainWidget
	: public Ui::RpWidget
	, public RPCSender
	, private base::Subscriber
	, private Media::Player::FloatDelegate {
	Q_OBJECT

public:
	using SectionShow = Window::SectionShow;

	MainWidget(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] bool isMainSectionShown() const;
	[[nodiscard]] bool isThirdSectionShown() const;

	[[nodiscard]] int contentScrollAddToY() const;

	void returnTabbedSelector();

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

	void start();

	void openPeerByName(
		const QString &name,
		MsgId msgId = ShowAtUnreadMsgId,
		const QString &startToken = QString(),
		FullMsgId clickFromMessageId = FullMsgId());

	bool started();

	void incrementSticker(DocumentData *sticker);

	void activate();
	[[nodiscard]] bool updateReceived(const mtpPrime *from, const mtpPrime *end);

	void refreshDialog(Dialogs::Key key);
	void removeDialog(Dialogs::Key key);
	void repaintDialogRow(Dialogs::Mode list, not_null<Dialogs::Row*> row);
	void repaintDialogRow(Dialogs::RowDescriptor row);

	void windowShown();

	void sentUpdatesReceived(uint64 randomId, const MTPUpdates &updates);
	void sentUpdatesReceived(const MTPUpdates &updates) {
		return sentUpdatesReceived(0, updates);
	}
	bool deleteChannelFailed(const RPCError &error);
	void historyToDown(History *hist);
	void dialogsToUp();
	void markActiveHistoryAsRead();

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

	void onSendFileConfirm(
		const std::shared_ptr<FileLoadResult> &file,
		const std::optional<FullMsgId> &oldId);
	bool onSendSticker(DocumentData *sticker);

	void updateOnlineDisplayIn(int32 msecs);

	bool isActive() const;
	bool doWeReadServerHistory() const;
	bool doWeReadMentions() const;
	bool lastWasOnline() const;
	crl::time lastSetOnline() const;

	void saveDraftToCloud();
	void applyCloudDraft(History *history);
	void writeDrafts(History *history);

	int32 dlgsWidth() const;

	void showForwardLayer(MessageIdsList &&items);
	void showSendPathsLayer();
	void cancelUploadLayer(not_null<HistoryItem*> item);
	void shareUrlLayer(const QString &url, const QString &text);
	void inlineSwitchLayer(const QString &botAndQuery);
	void hiderLayer(base::unique_qptr<Window::HistoryHider> h);
	bool setForwardDraft(PeerId peer, MessageIdsList &&items);
	bool shareUrl(
		PeerId peerId,
		const QString &url,
		const QString &text);
	void replyToItem(not_null<HistoryItem*> item);
	bool inlineSwitchChosen(PeerId peerId, const QString &botAndQuery);
	bool sendPaths(PeerId peerId);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer() const;

	void deletePhotoLayer(PhotoData *photo);

	// While HistoryInner is not HistoryView::ListWidget.
	crl::time highlightStartTime(not_null<const HistoryItem*> item) const;

	MsgId currentReplyToIdFor(not_null<History*> history) const;

	void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	void searchMessages(const QString &query, Dialogs::Key inChat);
	void itemEdited(not_null<HistoryItem*> item);

	void checkLastUpdate(bool afterSleep);

	bool isIdle() const;

	QPixmap cachedBackground(const QRect &forRect, int &x, int &y);
	void updateScrollColors();

	void setChatBackground(
		const Data::WallPaper &background,
		QImage &&image = QImage());
	bool chatBackgroundLoading();
	float64 chatBackgroundProgress() const;
	void checkChatBackground();
	Image *newBackgroundThumb();

	void messageDataReceived(ChannelData *channel, MsgId msgId);
	void updateBotKeyboard(History *h);

	void pushReplyReturn(not_null<HistoryItem*> item);

	void cancelForwarding(not_null<History*> history);
	void finishForwarding(Api::SendAction action);

	// Does offerPeer or showPeerHistory.
	void choosePeer(PeerId peerId, MsgId showAtMsgId);
	void clearBotStartToken(PeerData *peer);

	void ptsWaiterStartTimerFor(ChannelData *channel, int32 ms); // ms <= 0 - stop timer
	void feedUpdates(const MTPUpdates &updates, uint64 randomId = 0);

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	void feedChannelDifference(const MTPDupdates_channelDifference &data);

	// Made public for ApiWrap, while it is still here.
	// Better would be for this to be moved to ApiWrap.
	bool requestingDifference() const {
		return _ptsWaiter.requesting();
	}
	void getDifference();
	void updateOnline(bool gotOtherOffline = false);
	void checkIdleFinish();

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
	void notify_historyMuteUpdated(History *history);

	void closeBothPlayers();

	bool isQuitPrevent();

	~MainWidget();

signals:
	void dialogsUpdated();

public slots:
	void documentLoadProgress(FileLoader *loader);
	void documentLoadFailed(FileLoader *loader, bool started);
	void inlineResultLoadProgress(FileLoader *loader);
	void inlineResultLoadFailed(FileLoader *loader, bool started);

	void dialogsCancelled();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	using ChannelGetDifferenceTime = QMap<ChannelData*, crl::time>;
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

	void viewsIncrement();
	void sendPing();
	void getDifferenceByPts();
	void getDifferenceAfterFail();

	void animationCallback();
	void handleAdaptiveLayoutUpdate();
	void updateWindowAdaptiveLayout();
	void handleAudioUpdate(const Media::Player::TrackState &state);
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

	void setupConnectingWidget();
	void createPlayer();
	void playerHeightUpdated();

	void setCurrentCall(Calls::Call *call);
	void createCallTopBar();
	void destroyCallTopBar();
	void callTopBarHeightUpdated(int callTopBarHeight);

	void setCurrentExportView(Export::View::PanelController *view);
	void createExportTopBar(Export::View::Content &&data);
	void destroyExportTopBar();
	void exportTopBarHeightUpdated();

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

	void usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result);
	bool usernameResolveFail(QString name, const RPCError &error);

	int getMainSectionTop() const;
	int getThirdSectionTop() const;

	void hideAll();
	void showAll();
	void clearHider(not_null<Window::HistoryHider*> instance);

	void cacheBackground();
	void clearCachedBackground();

	not_null<Media::Player::FloatDelegate*> floatPlayerDelegate();
	not_null<Ui::RpWidget*> floatPlayerWidget() override;
	not_null<Window::SessionController*> floatPlayerController() override;
	not_null<Window::AbstractSectionWidget*> floatPlayerGetSection(
		Window::Column column) override;
	void floatPlayerEnumerateSections(Fn<void(
		not_null<Window::AbstractSectionWidget*> widget,
		Window::Column widgetColumn)> callback) override;
	bool floatPlayerIsVisible(not_null<HistoryItem*> item) override;
	void floatPlayerClosed(FullMsgId itemId);

	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, crl::time &curTime);

	void viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req);
	bool viewsIncrementFail(const RPCError &error, mtpRequestId req);

	void updateStatusDone(const MTPBool &result);
	bool updateStatusFail(const RPCError &error);

	void refreshResizeAreas();
	template <typename MoveCallback, typename FinishCallback>
	void createResizeArea(
		object_ptr<Ui::ResizeArea> &area,
		MoveCallback &&moveCallback,
		FinishCallback &&finishCallback);
	void ensureFirstColumnResizeAreaCreated();
	void ensureThirdColumnResizeAreaCreated();

	bool isReadyChatBackground(
		const Data::WallPaper &background,
		const QImage &image) const;
	void setReadyChatBackground(
		const Data::WallPaper &background,
		QImage &&image);

	void handleHistoryBack();

	not_null<Window::SessionController*> _controller;
	bool _started = false;

	Ui::Animations::Simple _a_show;
	bool _showBack = false;
	QPixmap _cacheUnder, _cacheOver;

	int _dialogsWidth = 0;
	int _thirdColumnWidth = 0;
	Ui::Animations::Simple _a_dialogsWidth;

	object_ptr<Ui::PlainShadow> _sideShadow;
	object_ptr<Ui::PlainShadow> _thirdShadow = { nullptr };
	object_ptr<Ui::ResizeArea> _firstColumnResizeArea = { nullptr };
	object_ptr<Ui::ResizeArea> _thirdColumnResizeArea = { nullptr };
	object_ptr<Dialogs::Widget> _dialogs;
	object_ptr<HistoryWidget> _history;
	object_ptr<Window::SectionWidget> _mainSection = { nullptr };
	object_ptr<Window::SectionWidget> _thirdSection = { nullptr };
	std::unique_ptr<Window::SectionMemento> _thirdSectionFromStack;
	std::unique_ptr<Window::ConnectionState> _connecting;

	base::weak_ptr<Calls::Call> _currentCall;
	object_ptr<Ui::SlideWrap<Calls::TopBar>> _callTopBar = { nullptr };

	Export::View::PanelController *_currentExportView = nullptr;
	object_ptr<Window::TopBarWrapWidget<Export::View::TopBar>> _exportTopBar
		= { nullptr };

	object_ptr<Window::TopBarWrapWidget<Media::Player::Widget>> _player
		= { nullptr };
	object_ptr<Media::Player::VolumeWidget> _playerVolume = { nullptr };
	object_ptr<Media::Player::Panel> _playerPlaylist;
	bool _playerUsingPanel = false;

	base::unique_qptr<Window::HistoryHider> _hider;
	std::vector<std::unique_ptr<StackItem>> _stack;

	int _playerHeight = 0;
	int _callTopBarHeight = 0;
	int _exportTopBarHeight = 0;
	int _contentScrollAddToY = 0;

	int32 updDate = 0;
	int32 updQts = -1;
	int32 updSeq = 0;
	base::Timer _noUpdatesTimer;

	PtsWaiter _ptsWaiter;

	ChannelGetDifferenceTime _channelGetDifferenceTimeByPts, _channelGetDifferenceTimeAfterFail;
	crl::time _getDifferenceTimeByPts = 0;
	crl::time _getDifferenceTimeAfterFail = 0;

	base::Timer _byPtsTimer;

	QMap<int32, MTPUpdates> _bySeqUpdates;
	base::Timer _bySeqTimer;

	base::Timer _byMinChannelTimer;

	mtpRequestId _onlineRequest = 0;
	base::Timer _onlineTimer;
	base::Timer _idleFinishTimer;
	bool _lastWasOnline = false;
	crl::time _lastSetOnline = 0;
	bool _isIdle = false;

	int32 _failDifferenceTimeout = 1; // growing timeout for getDifference calls, if it fails
	QMap<ChannelData*, int32> _channelFailDifferenceTimeout; // growing timeout for getChannelDifference calls, if it fails
	base::Timer _failDifferenceTimer;

	crl::time _lastUpdateTime = 0;
	bool _handlingChannelDifference = false;

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX = 0;
	int _cachedY = 0;
	base::Timer _cacheBackgroundTimer;

	PhotoData *_deletingPhoto = nullptr;

	using ViewsIncrementMap = QMap<MsgId, bool>;
	QMap<PeerData*, ViewsIncrementMap> _viewsIncremented, _viewsToIncrement;
	QMap<PeerData*, mtpRequestId> _viewsIncrementRequests;
	QMap<mtpRequestId, PeerData*> _viewsIncrementByRequest;
	base::Timer _viewsIncrementTimer;

	struct SettingBackground;
	std::unique_ptr<SettingBackground> _background;

	bool _firstColumnResizing = false;
	int _firstColumnResizingShift = 0;

};
