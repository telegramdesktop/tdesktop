/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Window {
class SessionController;
} // namespace Window

void ShowStickerPreviewBox(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document);
