/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

class UserData;

namespace Ui {
class RpWidget;
class SettingsButton;
class VerticalLayout;
struct UnreadBadgeStyle;
} // namespace Ui

namespace Main {
class Account;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type InformationId();

struct AccountsEvents {
	rpl::producer<> closeRequests;
	QPointer<Ui::RpWidget> addAccountButton;
};
AccountsEvents SetupAccounts(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

void UpdatePhotoLocally(not_null<UserData*> user, const QImage &image);

namespace Badge {

[[nodiscard]] Ui::UnreadBadgeStyle Style();

struct UnreadBadge {
	int count = 0;
	bool muted = false;
};
[[nodiscard]] not_null<Ui::RpWidget*> AddRight(
	not_null<Ui::SettingsButton*> button,
	int rightPadding = 0);
[[nodiscard]] not_null<Ui::RpWidget*> CreateUnread(
	not_null<Ui::RpWidget*> container,
	rpl::producer<UnreadBadge> value);
void AddUnread(
	not_null<Ui::SettingsButton*> button,
	rpl::producer<UnreadBadge> value);

} // namespace Badge
} // namespace Settings
