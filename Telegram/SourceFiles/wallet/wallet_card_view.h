/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Info::Profile {
class BalancePill;
} // namespace Info::Profile

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

void ShowCardView(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	not_null<Info::Profile::BalancePill*> origin);

} // namespace Wallet
