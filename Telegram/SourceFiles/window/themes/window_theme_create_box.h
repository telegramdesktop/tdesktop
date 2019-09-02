/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class GenericBox;

namespace Main {
class Session;
} // namespace Main

namespace Window {
namespace Theme {

void CreateBox(not_null<GenericBox*> box, not_null<Main::Session*> session);

} // namespace Theme
} // namespace Window
