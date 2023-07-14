/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
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
