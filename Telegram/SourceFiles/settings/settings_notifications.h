/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

class Notifications : public Section<Notifications> {
public:
	Notifications(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
