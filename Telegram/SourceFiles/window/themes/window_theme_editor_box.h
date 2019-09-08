/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/generic_box.h"

namespace Data {
struct CloudTheme;
} // namespace Data

namespace Window {

class Controller;

namespace Theme {

void StartEditor(
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud);
void CreateBox(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window);
void CreateForExistingBox(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud);
void SaveTheme(
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud,
	const QByteArray &palette,
	Fn<void()> unlock);
void SaveThemeBox(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud,
	const QByteArray &palette);

[[nodiscard]] bool PaletteChanged(
	const QByteArray &editorPalette,
	const Data::CloudTheme &cloud);

} // namespace Theme
} // namespace Window
