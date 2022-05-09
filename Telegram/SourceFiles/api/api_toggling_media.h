/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

void ToggleFavedSticker(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document,
	Data::FileOrigin origin);

void ToggleFavedSticker(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool faved);

void ToggleRecentSticker(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool saved);

void ToggleSavedGif(
	Window::SessionController *controller,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool saved);

void ToggleSavedRingtone(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	Fn<void()> &&done,
	bool saved);

} // namespace Api
