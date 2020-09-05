/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "settings/settings_common.h"

namespace Main {
class Session;
} // namespace Main

namespace Settings {

class Sessions : public Section {
public:
	Sessions(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings

class SessionsBox : public Ui::BoxContent {
public:
	SessionsBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;

private:
	const not_null<Main::Session*> _session;

};
