/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;
class GenericBox;

namespace Window {
class Controller;
} // namespace Window

void EditContactBox(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	not_null<UserData*> user);
