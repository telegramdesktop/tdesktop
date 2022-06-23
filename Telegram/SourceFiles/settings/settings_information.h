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

namespace Main {
class Account;
} // namespace Main

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

namespace Badge {

[[nodiscard]] Dialogs::Ui::UnreadBadgeStyle Style();

struct UnreadBadge {
	int count = 0;
	bool muted = false;
};
[[nodiscard]] not_null<Ui::RpWidget*> AddRight(
	not_null<Ui::SettingsButton*> button);
[[nodiscard]] not_null<Ui::RpWidget*> CreateUnread(
	not_null<Ui::RpWidget*> container,
	rpl::producer<UnreadBadge> value);
void AddUnread(
	not_null<Ui::SettingsButton*> button,
	rpl::producer<UnreadBadge> value);

} // namespace Badge
} // namespace Settings
