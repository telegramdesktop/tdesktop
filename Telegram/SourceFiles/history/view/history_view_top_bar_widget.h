/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/unread_badge.h"
#include "ui/effects/animations.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "data/data_report.h"
#include "dialogs/dialogs_key.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
class RoundButton;
class IconButton;
class PopupMenu;
class UnreadBadge;
class InputField;
class CrossButton;
class InfiniteRadialAnimation;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class SendActionPainter;

[[nodiscard]] QString SwitchToChooseFromQuery();

class TopBarWidget final : public Ui::RpWidget {
public:
	struct SelectedState {
		bool textSelected = false;
		int count = 0;
		int canDeleteCount = 0;
		int canForwardCount = 0;
		int canSendNowCount = 0;
	};
	using ActiveChat = Dialogs::EntryState;
	using Section = ActiveChat::Section;

	TopBarWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~TopBarWidget();

	[[nodiscard]] Main::Session &session() const;

	void updateControlsVisibility();
	void finishAnimating();
	void showSelected(SelectedState state);
	[[nodiscard]] bool showSelectedState() const;
	rpl::producer<bool> membersShowAreaActive() const {
		return _membersShowAreaActive.events();
	}
	void setAnimatingMode(bool enabled);

	void setActiveChat(
		ActiveChat activeChat,
		SendActionPainter *sendAction);
	void setCustomTitle(const QString &title);

	void showChooseMessagesForReport(Data::ReportInput reportInput);
	void clearChooseMessagesForReport();

	bool toggleSearch(bool shown, anim::type animated);
	void searchEnableJumpToDate(bool enable);
	void searchEnableChooseFromUser(bool enable, bool visible);
	bool searchSetFocus();
	[[nodiscard]] bool searchMode() const;
	[[nodiscard]] bool searchHasFocus() const;
	[[nodiscard]] rpl::producer<> searchCancelled() const;
	[[nodiscard]] rpl::producer<> searchSubmitted() const;
	[[nodiscard]] rpl::producer<QString> searchQuery() const;
	[[nodiscard]] QString searchQueryCurrent() const;
	[[nodiscard]] int searchQueryCursorPosition() const;
	void searchClear();
	void searchSetText(const QString &query, int cursorPosition = -1);

	[[nodiscard]] rpl::producer<> forwardSelectionRequest() const {
		return _forwardSelection.events();
	}
	[[nodiscard]] rpl::producer<> sendNowSelectionRequest() const {
		return _sendNowSelection.events();
	}
	[[nodiscard]] rpl::producer<> deleteSelectionRequest() const {
		return _deleteSelection.events();
	}
	[[nodiscard]] rpl::producer<> clearSelectionRequest() const {
		return _clearSelection.events();
	}
	[[nodiscard]] rpl::producer<> cancelChooseForReportRequest() const {
		return _cancelChooseForReport.events();
	}
	[[nodiscard]] rpl::producer<> jumpToDateRequest() const {
		return _jumpToDateRequests.events();
	}
	[[nodiscard]] rpl::producer<> chooseFromUserRequest() const {
		return _chooseFromUserRequests.events();
	}
	[[nodiscard]] rpl::producer<> searchRequest() const;

	void setGeometryWithNarrowRatio(
		QRect geometry,
		int narrowWidth,
		float64 narrowRatio);
	void showPeerMenu();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	struct EmojiInteractionSeenAnimation;

	[[nodiscard]] bool rootChatsListBar() const;
	void refreshInfoButton();
	void refreshLang();
	void updateSearchVisibility();
	void updateControlsGeometry();
	void slideAnimationCallback();
	void updateInfoToggleActive();
	void setupDragOnBackButton();

	void call();
	void groupCall();
	void showGroupCallMenu(not_null<PeerData*> peer);
	void toggleInfoSection();

	[[nodiscard]] bool createMenu(not_null<Ui::IconButton*> button);

	void handleEmojiInteractionSeen(const QString &emoticon);
	bool paintSendAction(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color fg,
		crl::time now);

	void updateConnectingState();
	void updateAdaptiveLayout();
	int countSelectedButtonsTop(float64 selectedShown);
	void connectingAnimationCallback();

	void paintTopBar(Painter &p);
	void paintStatus(
		Painter &p,
		int left,
		int top,
		int availableWidth,
		int outerWidth);
	bool paintConnectingState(Painter &p, int left, int top, int outerWidth);
	[[nodiscard]] QRect getMembersShowAreaGeometry() const;
	[[nodiscard]] bool trackOnlineOf(not_null<PeerData*> user) const;
	void updateMembersShowArea();
	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();
	void updateOnlineDisplayIn(crl::time timeout);

	void infoClicked();
	void backClicked();

	void refreshUnreadBadge();
	void updateUnreadBadge();
	void setChooseForReportReason(std::optional<Data::ReportInput>);
	void toggleSelectedControls(bool shown);
	[[nodiscard]] bool showSelectedActions() const;

	const not_null<Window::SessionController*> _controller;
	const bool _primaryWindow = false;
	ActiveChat _activeChat;
	QString _customTitleText;
	std::unique_ptr<EmojiInteractionSeenAnimation> _emojiInteractionSeen;
	rpl::lifetime _activeChatLifetime;

	Ui::PeerBadge _titleBadge;
	Ui::Text::String _title;
	int _titleNameVersion = 0;

	int _selectedCount = 0;
	bool _canDelete = false;
	bool _canForward = false;
	bool _canSendNow = false;
	bool _searchMode = false;

	Ui::Animations::Simple _selectedShown;
	Ui::Animations::Simple _searchShown;

	object_ptr<Ui::RoundButton> _clear;
	object_ptr<Ui::RoundButton> _forward, _sendNow, _delete;
	object_ptr<Ui::InputField> _searchField = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _chooseFromUser
		= { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _jumpToDate
		= { nullptr };
	object_ptr<Ui::CrossButton> _searchCancel = { nullptr };
	rpl::variable<QString> _searchQuery;
	rpl::event_stream<> _searchCancelled;
	rpl::event_stream<> _searchSubmitted;
	rpl::event_stream<> _jumpToDateRequests;
	rpl::event_stream<> _chooseFromUserRequests;

	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::IconButton> _cancelChoose;
	object_ptr<Ui::UnreadBadge> _unreadBadge = { nullptr };
	object_ptr<Ui::AbstractButton> _info = { nullptr };

	object_ptr<Ui::IconButton> _call;
	object_ptr<Ui::IconButton> _groupCall;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::IconButton> _infoToggle;
	object_ptr<Ui::IconButton> _menuToggle;
	base::unique_qptr<Ui::PopupMenu> _menu;

	object_ptr<TWidget> _membersShowArea = { nullptr };
	rpl::event_stream<bool> _membersShowAreaActive;

	float64 _narrowRatio = 0.;
	int _narrowWidth = 0;

	Ui::Text::String _titlePeerText;
	bool _titlePeerTextOnline = false;
	int _leftTaken = 0;
	int _rightTaken = 0;
	bool _animatingMode = false;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _connecting;

	SendActionPainter *_sendAction = nullptr;
	std::optional<Data::ReportInput> _chooseForReportReason;

	base::Timer _onlineUpdater;

	rpl::event_stream<> _forwardSelection;
	rpl::event_stream<> _sendNowSelection;
	rpl::event_stream<> _deleteSelection;
	rpl::event_stream<> _clearSelection;
	rpl::event_stream<> _cancelChooseForReport;

	rpl::lifetime _backLifetime;

};

} // namespace HistoryView
