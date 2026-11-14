/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class UserData;

namespace twidget {
struct WalletCall;
struct WidgetRequestOutcome;
} // namespace twidget

namespace Tgw {

[[nodiscard]] UserData *QueryBot(not_null<HistoryItem*> item);

void RunWalletCall(
	not_null<HistoryItem*> item,
	not_null<QWidget*> window,
	const twidget::WalletCall &call,
	Fn<void(const twidget::WidgetRequestOutcome&)> respond);

} // namespace Tgw
