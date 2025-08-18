/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

void AddSaveAudioAction(
	const Ui::Menu::MenuCallback &addAction,
	not_null<HistoryItem*> item,
	not_null<DocumentData*> document,
	not_null<Window::SessionController*> controller);

} // namespace HistoryView
