/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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

	void showFinished() override;

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	Fn<void()> _save;

	rpl::event_stream<> _showFinished;

};

} // namespace Settings

