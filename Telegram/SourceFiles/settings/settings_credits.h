/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

enum class PremiumFeature;

namespace style {
struct RoundButton;
} // namespace style

namespace ChatHelpers {
class Show;
enum class WindowUsage;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
class RoundButton;
class GradientButton;
class VerticalLayout;
} // namespace Ui

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type CreditsId();

} // namespace Settings

