/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

void ShowCardView(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	not_null<QWidget*> origin);

} // namespace Wallet
