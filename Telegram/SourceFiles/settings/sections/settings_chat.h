/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

class QPainter;
class QBrush;

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

[[nodiscard]] Type ChatId();

void SetupDataStorage(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupAutoDownload(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupDefaultThemes(
	not_null<Window::Controller*> window,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);
void SetupSupport(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupExport(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Type)> showOther);

void PaintRoundColorButton(
	QPainter &p,
	int size,
	QBrush brush,
	float64 selected);

void SetupThemeOptions(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupThemeSettings(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupCloudThemes(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupChatBackground(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupChatListQuickAction(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupStickersEmoji(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupMessages(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupArchive(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Type)> showOther);

void SetupSensitiveContent(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger,
	HighlightRegistry *highlights = nullptr);

} // namespace Settings
