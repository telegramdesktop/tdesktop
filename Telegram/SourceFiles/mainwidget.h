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
#include "mtproto/sender.h"
#include "data/data_pts_waiter.h"

struct HistoryMessageMarkupButton;
class MainWindow;
class ConfirmBox;
class HistoryWidget;
class StackItem;
struct FileLoadResult;
class History;
class Image;

namespace MTP {
class Error;
} // namespace MTP

namespace Api {
struct SendAction;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

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
enum class ReportReason;
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
class GroupCall;
class TopBar;
} // namespace Calls

namespace Core {
class Changelogs;
} // namespace Core

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace InlineBots

class MainWidget
	: public Ui::RpWidget
	, private Media::Player::FloatDelegate
	, private base::Subscriber {
	Q_OBJECT

public:
	using SectionShow = Window::SectionShow;

	MainWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~MainWidget();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<Window::SessionController*> controller() const;

	[[nodiscard]] bool isMainSectionShown() const;
	[[nodiscard]] bool isThirdSectionShown() const;

	void returnTabbedSelector();

	void showAnimated(const QPixmap &bgAnimCache, bool back = false);

	void activate();

	void windowShown();

	void dialogsToUp();
	void checkHistoryActivation();

	PeerData *peer();

	int backgroundFromY() const;
	void showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params);
	void updateColumnLayout();
	bool stackIsEmpty() const;
	void showBackFromStack(
		const SectionShow &params);
	void orderWidgets();
	QRect historyRect() const;
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);
	void checkMainSectionToLayer();

	bool sendExistingDocument(not_null<DocumentData*> sticker);

	bool isActive() const;
	[[nodiscard]] bool doWeMarkAsRead() const;

	void saveFieldToHistoryLocalDraft();

	int32 dlgsWidth() const;

	void showForwardLayer(MessageIdsList &&items);
	void showSendPathsLayer();
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

	void sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd,
		MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	void searchMessages(const QString &query, Dialogs::Key inChat);

	QPixmap cachedBackground(const QRect &forRect, int &x, int &y);

	void setChatBackground(
		const Data::WallPaper &background,
		QImage &&image = QImage());
	bool chatBackgroundLoading();
	float64 chatBackgroundProgress() const;
	void checkChatBackground();
	Image *newBackgroundThumb();

	// Does offerPeer or showPeerHistory.
	void choosePeer(PeerId peerId, MsgId showAtMsgId);
	void clearBotStartToken(PeerData *peer);

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	bool contentOverlapped(const QRect &globalRect);

	void searchInChat(Dialogs::Key chat);

	void showChooseReportMessages(
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> done);
	void clearChooseReportMessages();

	void ui_showPeerHistory(
		PeerId peer,
		const SectionShow &params,
		MsgId msgId);
	PeerData *ui_getPeerForMouseAction();

	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);

	using FloatDelegate::floatPlayerAreaUpdated;

	void closeBothPlayers();
	void stopAndClosePlayer();

	bool preventsCloseSection(Fn<void()> callback) const;
	bool preventsCloseSection(
		Fn<void()> callback,
		const SectionShow &params) const;

public Q_SLOTS:
	void inlineResultLoadProgress(FileLoader *loader);
	void inlineResultLoadFailed(FileLoader *loader, bool started);

	void dialogsCancelled();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void viewsIncrement();

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
		-> std::shared_ptr<Window::SectionMemento>;

	void setupConnectingWidget();
	void createPlayer();
	void playerHeightUpdated();

	void setCurrentCall(Calls::Call *call);
	void setCurrentGroupCall(Calls::GroupCall *call);
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
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params);
	void dropMainSection(Window::SectionWidget *widget);

	Window::SectionSlideParams prepareThirdSectionAnimation(Window::SectionWidget *section);

	// All this methods use the prepareShowAnimation().
	Window::SectionSlideParams prepareMainSectionAnimation(Window::SectionWidget *section);
	Window::SectionSlideParams prepareHistoryAnimation(PeerId historyPeerId);
	Window::SectionSlideParams prepareDialogsAnimation();

	void saveSectionInStack();

	int getMainSectionTop() const;
	int getThirdSectionTop() const;

	void hideAll();
	void showAll();
	void clearHider(not_null<Window::HistoryHider*> instance);

	void cacheBackground();
	void clearCachedBackground();

	[[nodiscard]] auto floatPlayerDelegate()
		-> not_null<Media::Player::FloatDelegate*>;
	not_null<Ui::RpWidget*> floatPlayerWidget() override;
	not_null<Media::Player::FloatSectionDelegate*> floatPlayerGetSection(
		Window::Column column) override;
	void floatPlayerEnumerateSections(Fn<void(
		not_null<Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) override;
	bool floatPlayerIsVisible(not_null<HistoryItem*> item) override;
	void floatPlayerClosed(FullMsgId itemId);
	void floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) override;

	void viewsIncrementDone(
		QVector<MTPint> ids,
		const MTPmessages_MessageViews &result,
		mtpRequestId requestId);
	void viewsIncrementFail(const MTP::Error &error, mtpRequestId requestId);

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

	bool isOneColumn() const;
	bool isNormalColumn() const;
	bool isThreeColumn() const;

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

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
	std::shared_ptr<Window::SectionMemento> _thirdSectionFromStack;
	std::unique_ptr<Window::ConnectionState> _connecting;

	base::weak_ptr<Calls::Call> _currentCall;
	base::weak_ptr<Calls::GroupCall> _currentGroupCall;
	rpl::lifetime _currentCallLifetime;
	object_ptr<Ui::SlideWrap<Calls::TopBar>> _callTopBar = { nullptr };

	Export::View::PanelController *_currentExportView = nullptr;
	object_ptr<Window::TopBarWrapWidget<Export::View::TopBar>> _exportTopBar
		= { nullptr };
	rpl::lifetime _exportViewLifetime;

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

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX = 0;
	int _cachedY = 0;
	base::Timer _cacheBackgroundTimer;

	PhotoData *_deletingPhoto = nullptr;

	base::flat_map<not_null<PeerData*>, base::flat_set<MsgId>> _viewsIncremented;
	base::flat_map<not_null<PeerData*>, base::flat_set<MsgId>> _viewsToIncrement;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _viewsIncrementRequests;
	base::flat_map<mtpRequestId, not_null<PeerData*>> _viewsIncrementByRequest;
	base::Timer _viewsIncrementTimer;

	struct SettingBackground;
	std::unique_ptr<SettingBackground> _background;

	bool _firstColumnResizing = false;
	int _firstColumnResizingShift = 0;

	// _changelogs depends on _data, subscribes on chats loading event.
	const std::unique_ptr<Core::Changelogs> _changelogs;

};

namespace App {
MainWidget *main();
} // namespace App
