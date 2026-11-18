#pragma once

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Window {

class Controller;

void ShowLogoutOptions(
	not_null<Controller*> window,
	not_null<Main::Session*> session);
void ShowLogoutConfirmation(
	not_null<Controller*> window,
	Main::Account *account);

} // namespace Window
