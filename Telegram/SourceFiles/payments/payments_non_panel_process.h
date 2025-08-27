/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Main {
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Payments {

enum class CheckoutResult;
struct CreditsFormData;
struct CreditsReceiptData;
struct NonPanelPaymentForm;

[[nodiscard]] bool IsCreditsInvoice(not_null<HistoryItem*> item);

void ProcessCreditsPayment(
	std::shared_ptr<Main::SessionShow> show,
	QPointer<QWidget> fireworks,
	std::shared_ptr<CreditsFormData> form,
	Fn<void(CheckoutResult)> maybeReturnToBot = nullptr);

void ProcessCreditsReceipt(
	not_null<Window::SessionController*> controller,
	std::shared_ptr<CreditsReceiptData> receipt,
	Fn<void(CheckoutResult)> maybeReturnToBot = nullptr);

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
	not_null<Window::SessionController*> controller,
	Fn<void(Payments::CheckoutResult)> maybeReturnToBot = nullptr);

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Payments
