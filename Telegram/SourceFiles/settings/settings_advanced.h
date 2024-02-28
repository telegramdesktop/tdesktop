/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Settings {

void SetupConnectionType(
	not_null<Window::Controller*> controller,
	not_null<::Main::Account*> account,
	not_null<Ui::VerticalLayout*> container);
bool HasUpdate();
void SetupUpdate(not_null<Ui::VerticalLayout*> container);
void SetupWindowTitleContent(
	Window::SessionController *controller,
	not_null<Ui::VerticalLayout*> container);
void SetupSystemIntegrationContent(
	Window::SessionController *controller,
	not_null<Ui::VerticalLayout*> container);
void SetupAnimations(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container);

void ArchiveSettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);
void PreloadArchiveSettings(not_null<::Main::Session*> session);

class Advanced : public Section<Advanced> {
public:
	Advanced(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
