/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Info {
namespace Profile {

[[nodiscard]] bool IsCollectiblePhone(not_null<UserData*> user);

void AddPhoneMenu(not_null<Ui::PopupMenu*> menu, not_null<UserData*> user);

} // namespace Profile
} // namespace Info
