/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {

class GenericBox;

void SendCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryItem*> item);

[[nodiscard]] bool IsCreditsInvoice(not_null<HistoryItem*> item);

} // namespace Ui
