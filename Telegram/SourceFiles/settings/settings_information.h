/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Dialogs::Ui {
struct UnreadBadgeStyle;
} // namespace Dialogs::Ui

namespace Settings {

class Information : public Section<Information> {
public:
	Information(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

struct AccountsEvents {
	rpl::producer<> currentAccountActivations;
};
AccountsEvents SetupAccounts(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

[[nodiscard]] Dialogs::Ui::UnreadBadgeStyle BadgeStyle();

struct UnreadBadge {
	int count = 0;
	bool muted = false;
};
void AddUnreadBadge(
	not_null<Ui::SettingsButton*> button,
	rpl::producer<UnreadBadge> value);

} // namespace Settings
