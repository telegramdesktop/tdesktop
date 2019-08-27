/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#pragma once

#include "settings/settings_common.h"
#include "ui/rp_widget.h"

namespace Settings {

void GreatSetting(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

} // namespace Settings
