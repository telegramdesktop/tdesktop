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

class Sessions : public Section<Sessions> {
public:
	Sessions(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] static rpl::producer<QString> Title();

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings

class SessionsBox : public Ui::BoxContent {
public:
	SessionsBox(QWidget*, not_null<Window::SessionController*> controller);

protected:
	void prepare() override;

private:
	const not_null<Window::SessionController*> _controller;

};
