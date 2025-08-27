/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

void SetupLanguageButton(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container);
bool HasInterfaceScale();
void SetupInterfaceScale(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container,
	bool icon = true);

void OpenFaq(base::weak_ptr<Window::SessionController> weak);

class Main : public Section<Main> {
public:
	Main(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	void fillTopBarMenu(
		const Ui::Menu::MenuCallback &addAction) override;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	const not_null<Window::SessionController*> _controller;

};

} // namespace Settings
