/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

void SaveNewFilterPinned(
	not_null<Main::Session*> session,
	FilterId filterId);

void CheckFilterInvite(
	not_null<Window::SessionController*> controller,
	const QString &slug);

} // namespace Api
