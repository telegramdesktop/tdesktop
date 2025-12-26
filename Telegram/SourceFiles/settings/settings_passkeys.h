/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Settings {

void PasskeysNoneBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

Type PasskeysId();

} // namespace Settings
