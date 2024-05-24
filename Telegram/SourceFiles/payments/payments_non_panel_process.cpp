/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_non_panel_process.h"

#include "base/unixtime.h"
#include "boxes/send_credits_box.h"
#include "data/data_credits.h"
#include "data/data_photo.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "payments/payments_checkout_process.h" // NonPanelPaymentForm.
#include "payments/payments_form.h"
#include "settings/settings_credits.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"

namespace Payments {
namespace {

bool IsCreditsInvoice(not_null<HistoryItem*> item) {
	if (const auto payment = item->Get<HistoryServicePayment>()) {
		return payment->isCreditsCurrency;
	}
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	return invoice && (invoice->currency == Ui::kCreditsCurrency);
}

} // namespace

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
		not_null<Window::SessionController*> controller) {
	return [=](NonPanelPaymentForm form) {
		using CreditsFormDataPtr = std::shared_ptr<CreditsFormData>;
		using CreditsReceiptPtr = std::shared_ptr<CreditsReceiptData>;
		if (const auto creditsData = std::get_if<CreditsFormDataPtr>(&form)) {
			controller->uiShow()->show(Box(Ui::SendCreditsBox, *creditsData));
		}
		if (const auto r = std::get_if<CreditsReceiptPtr>(&form)) {
			const auto receipt = *r;
			const auto entry = Data::CreditsHistoryEntry{
				.id = receipt->id,
				.title = receipt->title,
				.description = receipt->description,
				.date = base::unixtime::parse(receipt->date),
				.photoId = receipt->photo ? receipt->photo->id : 0,
				.credits = receipt->credits,
				.bareId = receipt->peerId.value,
				.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
			};
			controller->uiShow()->show(Box(
				Settings::ReceiptCreditsBox,
				controller,
				nullptr,
				entry));
		}
	};
}

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	return IsCreditsInvoice(item)
		? ProcessNonPanelPaymentFormFactory(controller)
		: nullptr;
}

} // namespace Payments
