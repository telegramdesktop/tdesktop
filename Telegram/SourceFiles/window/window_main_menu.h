/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Ui {
class IconButton;
class FlatLabel;
class Menu;
class UserpicButton;
} // namespace Ui

namespace Window {

class Controller;

class MainMenu : public TWidget, private base::Subscriber {
public:
	MainMenu(QWidget *parent, not_null<Controller*> controller);

	void setInnerFocus() {
		setFocus();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void checkSelf();
	void updateControlsGeometry();
	void updatePhone();
	void refreshMenu();

	not_null<Controller*> _controller;
	object_ptr<Ui::UserpicButton> _userpicButton = { nullptr };
	object_ptr<Ui::IconButton> _cloudButton = { nullptr };
	object_ptr<Ui::Menu> _menu;
	object_ptr<Ui::FlatLabel> _telegram;
	object_ptr<Ui::FlatLabel> _version;
	std::shared_ptr<QPointer<QAction>> _nightThemeAction;
	base::Timer _nightThemeSwitch;

	QString _phoneText;

};

} // namespace Window
