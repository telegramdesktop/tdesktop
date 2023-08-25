/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Settings {

class Notifications : public Section<Notifications> {
public:
	Notifications(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	rpl::producer<Type> sectionShowOther() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	rpl::event_stream<Type> _showOther;

};

} // namespace Settings
