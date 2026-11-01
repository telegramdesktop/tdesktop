/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QKeyEvent;

namespace Core {
class Application;
} // namespace Core

namespace Window {
class Controller;
} // namespace Window

void ShowKeysLegend(not_null<Window::Controller*> window);

[[nodiscard]] bool HandleKeysLegendHotkey(
	not_null<Core::Application*> app,
	not_null<QObject*> object,
	QKeyEvent *event);
