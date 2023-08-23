/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct ClickHandlerContext;
class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

void SendBotCallbackData(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	int row,
	int column);

void SendBotCallbackDataWithPassword(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	int row,
	int column);

bool SwitchInlineBotButtonReceived(
	not_null<Window::SessionController*> controller,
	const QByteArray &queryWithPeerTypes,
	UserData *samePeerBot = nullptr,
	MsgId samePeerReplyTo = 0);

void ActivateBotCommand(ClickHandlerContext context, int row, int column);

} // namespace Api
