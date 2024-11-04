/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "media/player/media_player_float.h"
#include "mtproto/sender.h"

class HistoryWidget;
class StackItem;
class Image;

namespace Bot {
struct SendCommandRequest;
} // namespace Bot

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Thread;
class WallPaper;
struct ForwardDraft;
class Forum;
struct ReportInput;
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
class ChatTheme;
class ResizeArea;
class PlainShadow;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
template <typename Inner>
class TopBarWrapWidget;
class SectionMemento;
class SectionWidget;
class SlideAnimation;
class ConnectionState;
struct SectionSlideParams;
struct SectionShow;
enum class Column;
class HistoryHider;
struct SeparateId;
} // namespace Window

namespace Calls {
class Call;
class GroupCall;
class TopBar;
} // namespace Calls

namespace Core {
class Changelogs;
} // namespace Core

class MainWidget final
	: public Ui::RpWidget
	, private Media::Player::FloatDelegate {
public:
	using SectionShow = Window::SectionShow;

	MainWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~MainWidget();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<Window::SessionController*> controller() const;
	[[nodiscard]] Window::SeparateId windowId() const;
	[[nodiscard]] bool isPrimary() const;
	[[nodiscard]] bool isMainSectionShown() const;
	[[nodiscard]] bool isThirdSectionShown() const;

	[[nodiscard]] Dialogs::RowDescriptor resolveChatNext(
		Dialogs::RowDescriptor from) const;
	[[nodiscard]] Dialogs::RowDescriptor resolveChatPrevious(
		Dialogs::RowDescriptor from) const;

	void returnTabbedSelector();

	void showAnimated(QPixmap oldContentCache, bool back = false);

	void activate();

	void windowShown();

	void dialogsToUp();
	void checkActivation();

	[[nodiscard]] PeerData *peer() const;
	[[nodiscard]] Ui::ChatTheme *customChatTheme() const;

	int backgroundFromY() const;
	void showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params);
	void updateColumnLayout();
	bool stackIsEmpty() const;
	bool showBackFromStack(const SectionShow &params);
	void orderWidgets();
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);
	void checkMainSectionToLayer();

	[[nodiscard]] SendMenu::Details sendMenuDetails() const;

	[[nodiscard]] bool animatingShow() const;

	void showDragForwardInfo();
	void hideDragForwardInfo();

	bool setForwardDraft(
		not_null<Data::Thread*> thread,
		Data::ForwardDraft &&draft);
	bool sendPaths(
		not_null<Data::Thread*> thread,
		const QStringList &paths);
	bool shareUrl(
		not_null<Data::Thread*> thread,
		const QString &url,
		const QString &text) const;
	bool filesOrForwardDrop(
		not_null<Data::Thread*> thread,
		not_null<const QMimeData*> data);

	void sendBotCommand(Bot::SendCommandRequest request);
	void hideSingleUseKeyboard(FullMsgId replyToId);

	void searchMessages(
		const QString &query,
		Dialogs::Key inChat,
		PeerData *searchFrom = nullptr);

	void setChatBackground(
		const Data::WallPaper &background,
		QImage &&image = QImage());
	bool chatBackgroundLoading();
	float64 chatBackgroundProgress() const;
	void checkChatBackground();
	Image *newBackgroundThumb();

	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void showChooseReportMessages(
		not_null<PeerData*> peer,
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> done);
	void clearChooseReportMessages();

	void toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show);

	void showHistory(
		PeerId peer,
		const SectionShow &params,
		MsgId msgId);
	void showMessage(
		not_null<const HistoryItem*> item,
		const SectionShow &params);
	void showForum(not_null<Data::Forum*> forum, const SectionShow &params);

	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);

	using FloatDelegate::floatPlayerAreaUpdated;

	void stopAndClosePlayer();

	bool preventsCloseSection(Fn<void()> callback) const;
	bool preventsCloseSection(
		Fn<void()> callback,
		const SectionShow &params) const;

	void showNonPremiumLimitToast(bool download);

	void dialogsCancelled();
	void toggleFiltersMenu(bool value) const;

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

	[[nodiscard]] bool relevantForDialogsFocus(
		not_null<QWidget*> widget) const;

	void showFinished();
	void handleAdaptiveLayoutUpdate();
	void updateWindowAdaptiveLayout();
	void handleAudioUpdate(const Media::Player::TrackState &state);
	void updateMediaPlaylistPosition(int x);
	void updateControlsGeometry();
	void updateMainSectionShown();
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
	void destroyThirdSection();

	Window::SectionSlideParams prepareThirdSectionAnimation(Window::SectionWidget *section);

	// All this methods use the prepareShowAnimation().
	Window::SectionSlideParams prepareMainSectionAnimation(Window::SectionWidget *section);
	Window::SectionSlideParams prepareHistoryAnimation(PeerId historyPeerId);
	Window::SectionSlideParams prepareDialogsAnimation();

	bool saveSectionInStack(
		const SectionShow &params,
		Window::SectionWidget *newMainSection = nullptr);

	int getMainSectionTop() const;
	int getThirdSectionTop() const;

	void hideAll();
	void showAll();
	void hiderLayer(base::unique_qptr<Window::HistoryHider> h);
	void clearHider(not_null<Window::HistoryHider*> instance);

	void closeBothPlayers();

	[[nodiscard]] auto floatPlayerDelegate()
		-> not_null<Media::Player::FloatDelegate*>;
	not_null<Ui::RpWidget*> floatPlayerWidget() override;
	void floatPlayerToggleGifsPaused(bool paused) override;
	not_null<Media::Player::FloatSectionDelegate*> floatPlayerGetSection(
		Window::Column column) override;
	void floatPlayerEnumerateSections(Fn<void(
		not_null<Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) override;
	bool floatPlayerIsVisible(not_null<HistoryItem*> item) override;
	void floatPlayerClosed(FullMsgId itemId);
	void floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) override;

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
	bool showHistoryInDifferentWindow(
		PeerId peerId,
		const SectionShow &params,
		MsgId showAtMsgId);

	bool isOneColumn() const;
	bool isNormalColumn() const;
	bool isThreeColumn() const;

	const not_null<Window::SessionController*> _controller;

	std::unique_ptr<Window::SlideAnimation> _showAnimation;

	int _dialogsWidth = 0;
	int _thirdColumnWidth = 0;
	Ui::Animations::Simple _a_dialogsWidth;

	const base::unique_qptr<Dialogs::Widget> _dialogs;
	const base::unique_qptr<HistoryWidget> _history;
	object_ptr<Window::SectionWidget> _mainSection = { nullptr };
	object_ptr<Window::SectionWidget> _thirdSection = { nullptr };
	std::shared_ptr<Window::SectionMemento> _thirdSectionFromStack;
	std::unique_ptr<Window::ConnectionState> _connecting;

	const base::unique_qptr<Ui::PlainShadow> _sideShadow;
	object_ptr<Ui::PlainShadow> _thirdShadow = { nullptr };
	object_ptr<Ui::ResizeArea> _firstColumnResizeArea = { nullptr };
	object_ptr<Ui::ResizeArea> _thirdColumnResizeArea = { nullptr };

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
	object_ptr<Media::Player::Panel> _playerPlaylist;

	base::unique_qptr<Window::HistoryHider> _hider;
	std::vector<std::unique_ptr<StackItem>> _stack;

	int _playerHeight = 0;
	int _callTopBarHeight = 0;
	int _exportTopBarHeight = 0;
	int _contentScrollAddToY = 0;

	struct SettingBackground;
	std::unique_ptr<SettingBackground> _background;

	// _changelogs depends on _data, subscribes on chats loading event.
	const std::unique_ptr<Core::Changelogs> _changelogs;

};
