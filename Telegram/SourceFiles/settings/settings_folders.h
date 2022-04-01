/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Settings {

class Folders : public Section<Folders> {
public:
	Folders(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Folders();

	[[nodiscard]] static rpl::producer<QString> Title();

private:
	void setupContent(not_null<Window::SessionController*> controller);

	Fn<void()> _save;

};

} // namespace Settings

