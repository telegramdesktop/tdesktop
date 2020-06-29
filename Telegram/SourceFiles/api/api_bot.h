/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Api {

void SendBotCallbackData(
	not_null<HistoryItem*> item,
	int row,
	int column);

} // namespace Api
