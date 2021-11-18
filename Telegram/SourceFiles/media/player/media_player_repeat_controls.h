/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Player {

class Dropdown;

void PrepareRepeatDropdown(
	not_null<Dropdown*> dropdown,
	not_null<Window::SessionController*> controller);

} // namespace Media::Player
