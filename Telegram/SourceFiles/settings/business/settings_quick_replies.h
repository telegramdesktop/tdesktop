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

namespace Settings {

[[nodiscard]] Type QuickRepliesId();

void EditShortcutNameBox(
	not_null<Ui::GenericBox*> box,
	QString name,
	Fn<void(QString, Fn<void()>)> submit);

} // namespace Settings
