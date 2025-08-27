/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Settings {

enum class ScalePreviewShow {
	Show,
	Update,
	Hide,
};

[[nodiscard]] Fn<void(ScalePreviewShow, int, int)> SetupScalePreview(
	not_null<Window::Controller*> window,
	not_null<Ui::RpWidget*> slider);

} // namespace Settings
