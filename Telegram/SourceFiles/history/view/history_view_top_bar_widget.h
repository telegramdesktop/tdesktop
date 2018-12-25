/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/timer.h"
#include "dialogs/dialogs_key.h"

namespace Ui {
class AbstractButton;
class RoundButton;
class IconButton;
class DropdownMenu;
class UnreadBadge;
class InfiniteRadialAnimation;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace HistoryView {

class TopBarWidget : public Ui::RpWidget, private base::Subscriber {
public:
	TopBarWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller);

	struct SelectedState {
		bool textSelected = false;
		int count = 0;
		int canDeleteCount = 0;
		int canForwardCount = 0;
	};

	~TopBarWidget();

	void updateControlsVisibility();
	void finishAnimating();
	void showSelected(SelectedState state);
	rpl::producer<bool> membersShowAreaActive() const {
		return _membersShowAreaActive.events();
	}
	void setAnimatingMode(bool enabled);

	void setActiveChat(Dialogs::Key chat);

	rpl::producer<> forwardSelectionRequest() const {
		return _forwardSelection.events();
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
	void updateControlsGeometry();
	void selectedShowCallback();
	void updateInfoToggleActive();

	void onCall();
	void onSearch();
	void showMenu();
	void toggleInfoSection();

	void updateConnectingState();
	void updateAdaptiveLayout();
	int countSelectedButtonsTop(float64 selectedShown);
	void step_connecting(TimeMs ms, bool timer);

	void paintTopBar(Painter &p, TimeMs ms);
	void paintStatus(
		Painter &p,
		int left,
		int top,
		int availableWidth,
		int outerWidth);
	bool paintConnectingState(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms);
	QRect getMembersShowAreaGeometry() const;
	void updateMembersShowArea();
	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();
	void updateOnlineDisplayIn(TimeMs timeout);

	void infoClicked();
	void backClicked();

	void createUnreadBadge();
	void updateUnreadBadge();

	not_null<Window::Controller*> _controller;
	Dialogs::Key _activeChat;

	int _selectedCount = 0;
	bool _canDelete = false;
	bool _canForward = false;

	Animation _selectedShown;

	object_ptr<Ui::RoundButton> _clear;
	object_ptr<Ui::RoundButton> _forward, _delete;

	object_ptr<Ui::IconButton> _back;
	object_ptr<Ui::UnreadBadge> _unreadBadge = { nullptr };
	object_ptr<Ui::AbstractButton> _info = { nullptr };

	object_ptr<Ui::IconButton> _call;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::IconButton> _infoToggle;
	object_ptr<Ui::IconButton> _menuToggle;
	object_ptr<Ui::DropdownMenu> _menu = { nullptr };

	object_ptr<TWidget> _membersShowArea = { nullptr };
	rpl::event_stream<bool> _membersShowAreaActive;

	Text _titlePeerText;
	bool _titlePeerTextOnline = false;
	int _leftTaken = 0;
	int _rightTaken = 0;
	bool _animatingMode = false;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _connecting;

	int _unreadCounterSubscription = 0;
	base::Timer _onlineUpdater;

	rpl::event_stream<> _forwardSelection;
	rpl::event_stream<> _deleteSelection;
	rpl::event_stream<> _clearSelection;

};

} // namespace HistoryView
