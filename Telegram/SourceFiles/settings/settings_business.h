/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

enum class PremiumFeature;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type BusinessId();

void ShowBusiness(not_null<Window::SessionController*> controller);

[[nodiscard]] std::vector<PremiumFeature> BusinessFeaturesOrder(
	not_null<::Main::Session*> session);

} // namespace Settings
