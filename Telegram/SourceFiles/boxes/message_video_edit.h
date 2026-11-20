/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Ui {
struct PreparedList;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

[[nodiscard]] DocumentData *FindEditableMessageVideo(HistoryItem *item);

void EditMessageVideo(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId,
	Fn<void(Ui::PreparedList&&)> done);
