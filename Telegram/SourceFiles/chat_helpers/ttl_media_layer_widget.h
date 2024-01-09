/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {

void ShowTTLMediaLayerWidget(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace ChatHelpers
