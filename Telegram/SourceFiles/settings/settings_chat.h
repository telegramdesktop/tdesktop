/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Window {
class Controller;
} // namespace Window

namespace Settings {

void SetupDataStorage(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupAutoDownload(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupDefaultThemes(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container);
void SetupSupport(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

class Chat : public Section {
public:
	Chat(QWidget *parent, not_null<Window::SessionController*> controller);

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
