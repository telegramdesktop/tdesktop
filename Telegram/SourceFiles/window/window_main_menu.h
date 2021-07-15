/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/object_ptr.h"
#include "base/binary_guard.h"
#include "ui/rp_widget.h"
#include "ui/layers/layer_widget.h"

namespace Ui {
class IconButton;
class FlatLabel;
class UserpicButton;
class PopupMenu;
class ScrollArea;
class VerticalLayout;
class RippleButton;
class PlainShadow;
template <typename Widget>
class SlideWrap;
namespace Menu {
class Menu;
} // namespace Menu
} // namespace Ui

namespace Main {
class Account;
} // namespace Main

namespace Window {

class SessionController;

class MainMenu final : public Ui::LayerWidget {
public:
	MainMenu(QWidget *parent, not_null<SessionController*> controller);

	void parentResized() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override {
		setFocus();
	}

private:
	class AccountButton;
	class ToggleAccountsButton;
	class ResetScaleButton;

	void setupArchiveButton();
	void setupCloudButton();
	void setupUserpicButton();
	void setupAccounts();
	void setupAccountsToggle();
	[[nodiscard]] not_null<Ui::SlideWrap<Ui::RippleButton>*> setupAddAccount(
		not_null<Ui::VerticalLayout*> container);
	void rebuildAccounts();
	void updateControlsGeometry();
	void updateInnerControlsGeometry();
	void updatePhone();
	void initResetScaleButton();
	void refreshMenu();
	void refreshBackground();
	void toggleAccounts();

	const not_null<SessionController*> _controller;
	object_ptr<Ui::UserpicButton> _userpicButton;
	object_ptr<ToggleAccountsButton> _toggleAccounts;
	object_ptr<Ui::IconButton> _archiveButton;
	object_ptr<Ui::IconButton> _cloudButton;
	object_ptr<ResetScaleButton> _resetScaleButton = { nullptr };
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _inner;
	base::flat_map<
		not_null<Main::Account*>,
		base::unique_qptr<AccountButton>> _watched;
	not_null<Ui::SlideWrap<Ui::VerticalLayout>*> _accounts;
	Ui::SlideWrap<Ui::RippleButton> *_addAccount = nullptr;
	not_null<Ui::SlideWrap<Ui::PlainShadow>*> _shadow;
	not_null<Ui::Menu::Menu*> _menu;
	not_null<Ui::RpWidget*> _footer;
	not_null<Ui::FlatLabel*> _telegram;
	not_null<Ui::FlatLabel*> _version;
	std::shared_ptr<QPointer<QAction>> _nightThemeAction;
	base::Timer _nightThemeSwitch;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

	base::binary_guard _accountSwitchGuard;

	QString _phoneText;
	QImage _background;

};

struct OthersUnreadState {
	int count = 0;
	bool allMuted = false;
};

[[nodiscard]] OthersUnreadState OtherAccountsUnreadStateCurrent();
[[nodiscard]] rpl::producer<OthersUnreadState> OtherAccountsUnreadState();

} // namespace Window
