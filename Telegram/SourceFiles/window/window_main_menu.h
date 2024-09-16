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
#include "ui/unread_badge.h"
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
class SettingsButton;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info::Profile {
class Badge;
class EmojiStatusPanel;
} // namespace Info::Profile

namespace Main {
class Account;
} // namespace Main

namespace Window {

class SessionController;

class MainMenu final : public Ui::LayerWidget {
public:
	MainMenu(QWidget *parent, not_null<SessionController*> controller);
	~MainMenu();

	void parentResized() override;
	void showFinished() override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override {
		setFocus();
	}

private:
	class ToggleAccountsButton;
	class ResetScaleButton;

	void moveBadge();
	void setupUserpicButton();
	void setupAccounts();
	void setupAccountsToggle();
	void setupSetEmojiStatus();
	void setupArchive();
	void setupMenu();
	void updateControlsGeometry();
	void updateInnerControlsGeometry();
	void initResetScaleButton();
	void toggleAccounts();
	void chooseEmojiStatus();

	void drawName(Painter &p);

	const not_null<SessionController*> _controller;
	object_ptr<Ui::UserpicButton> _userpicButton;
	Ui::Text::String _name;
	int _nameVersion = 0;
	object_ptr<ToggleAccountsButton> _toggleAccounts;
	object_ptr<Ui::FlatLabel> _setEmojiStatus;
	std::unique_ptr<Info::Profile::EmojiStatusPanel> _emojiStatusPanel;
	std::unique_ptr<Info::Profile::Badge> _badge;
	object_ptr<ResetScaleButton> _resetScaleButton = { nullptr };
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _inner;
	not_null<Ui::RpWidget*> _topShadowSkip;
	not_null<Ui::SlideWrap<Ui::VerticalLayout>*> _accounts;
	not_null<Ui::SlideWrap<Ui::PlainShadow>*> _shadow;
	not_null<Ui::VerticalLayout*> _menu;
	not_null<Ui::RpWidget*> _footer;
	not_null<Ui::FlatLabel*> _telegram;
	not_null<Ui::FlatLabel*> _version;
	QPointer<Ui::SettingsButton> _nightThemeToggle;
	rpl::event_stream<bool> _nightThemeSwitches;
	base::Timer _nightThemeSwitch;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

	rpl::variable<bool> _showFinished = false;

};

struct OthersUnreadState {
	int count = 0;
	bool allMuted = false;
};

[[nodiscard]] OthersUnreadState OtherAccountsUnreadStateCurrent(
	not_null<Main::Account*> current);
[[nodiscard]] rpl::producer<OthersUnreadState> OtherAccountsUnreadState(
	not_null<Main::Account*> current);

} // namespace Window
