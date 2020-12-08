/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "dialogs/dialogs_key.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AbstractButton;
class RoundButton;
class IconButton;
class DropdownMenu;
class UnreadBadge;
class InfiniteRadialAnimation;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class SendActionPainter;

class TopBarWidget : public Ui::RpWidget, private base::Subscriber {
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
	rpl::producer<bool> membersShowAreaActive() const {
		return _membersShowAreaActive.events();
	}
	void setAnimatingMode(bool enabled);

	void setActiveChat(
		ActiveChat activeChat,
		SendActionPainter *sendAction);
	void setCustomTitle(const QString &title);

	rpl::producer<> forwardSelectionRequest() const {
		return _forwardSelection.events();
	}
	rpl::producer<> sendNowSelectionRequest() const {
		return _sendNowSelection.events();
	}
	rpl::producer<> deleteSelectionRequest() const {
		return _deleteSelection.events();
	}
	rpl::producer<> clearSelectionRequest() const {
		return _clearSelection.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	void refreshInfoButton();
	void refreshLang();
	void updateSearchVisibility();
	void updateControlsGeometry();
	void selectedShowCallback();
	void updateInfoToggleActive();

	void call();
	void groupCall();
	void startGroupCall(not_null<ChannelData*> megagroup, bool confirmed);
	void search();
	void showMenu();
	void toggleInfoSection();

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
	QRect getMembersShowAreaGeometry() const;
	void updateMembersShowArea();
	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();
	void updateOnlineDisplayIn(crl::time timeout);

	void infoClicked();
	void backClicked();

	void refreshUnreadBadge();
	void updateUnreadBadge();

	const not_null<Window::SessionController*> _controller;
	ActiveChat _activeChat;
	QString _customTitleText;

	int _selectedCount = 0;
	bool _canDelete = false;
	bool _canForward = false;
	bool _canSendNow = false;

	Ui::Animations::Simple _selectedShown;

	object_ptr<Ui::RoundButton> _clear;
	object_ptr<Ui::RoundButton> _forward, _sendNow, _delete;

	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::UnreadBadge> _unreadBadge = { nullptr };
	object_ptr<Ui::AbstractButton> _info = { nullptr };

	object_ptr<Ui::IconButton> _call;
	object_ptr<Ui::IconButton> _groupCall;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::IconButton> _infoToggle;
	object_ptr<Ui::IconButton> _menuToggle;
	object_ptr<Ui::DropdownMenu> _menu = { nullptr };

	object_ptr<TWidget> _membersShowArea = { nullptr };
	rpl::event_stream<bool> _membersShowAreaActive;

	Ui::Text::String _titlePeerText;
	bool _titlePeerTextOnline = false;
	int _leftTaken = 0;
	int _rightTaken = 0;
	bool _animatingMode = false;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _connecting;

	SendActionPainter *_sendAction = nullptr;

	base::Timer _onlineUpdater;

	rpl::event_stream<> _forwardSelection;
	rpl::event_stream<> _sendNowSelection;
	rpl::event_stream<> _deleteSelection;
	rpl::event_stream<> _clearSelection;

};

} // namespace HistoryView
