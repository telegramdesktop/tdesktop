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

namespace Payments {

enum class CheckoutResult;
struct NonPanelPaymentForm;

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
	not_null<Window::SessionController*> controller,
	Fn<void(Payments::CheckoutResult)> maybeReturnToBot = nullptr);

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Payments
