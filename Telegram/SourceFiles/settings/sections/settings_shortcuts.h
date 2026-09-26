/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Shortcuts {
enum class Command;
} // namespace Shortcuts

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type ShortcutsId();
[[nodiscard]] QString ShortcutsHighlightId(::Shortcuts::Command command);

} // namespace Settings
