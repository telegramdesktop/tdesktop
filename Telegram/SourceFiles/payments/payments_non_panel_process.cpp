/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_non_panel_process.h"

#include "api/api_credits.h"
#include "base/unixtime.h"
#include "boxes/send_credits_box.h"
#include "data/data_credits.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "payments/payments_checkout_process.h" // NonPanelPaymentForm.
#include "payments/payments_form.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
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
		not_null<Window::SessionController*> controller,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	return [=](NonPanelPaymentForm form) {
		using CreditsFormDataPtr = std::shared_ptr<CreditsFormData>;
		using CreditsReceiptPtr = std::shared_ptr<CreditsReceiptData>;
		if (const auto creditsData = std::get_if<CreditsFormDataPtr>(&form)) {
			const auto form = *creditsData;
			const auto lifetime = std::make_shared<rpl::lifetime>();
			const auto api = lifetime->make_state<Api::CreditsStatus>(
				controller->session().user());
			const auto sendBox = [=, weak = base::make_weak(controller)] {
				if (const auto strong = weak.get()) {
					const auto unsuccessful = std::make_shared<bool>(true);
					const auto box = controller->uiShow()->show(Box(
						Ui::SendCreditsBox,
						form,
						crl::guard(strong, [=] {
							*unsuccessful = false;
							Ui::StartFireworks(strong->content());
							if (maybeReturnToBot) {
								maybeReturnToBot(CheckoutResult::Paid);
							}
						})));
					box->boxClosing() | rpl::start_with_next([=] {
						crl::on_main([=] {
							if ((*unsuccessful) && maybeReturnToBot) {
								maybeReturnToBot(CheckoutResult::Cancelled);
							}
						});
					}, box->lifetime());
				}
			};
			const auto weak = base::make_weak(controller);
			api->request({}, [=](Data::CreditsStatusSlice slice) {
				if (const auto strong = weak.get()) {
					strong->session().setCredits(slice.balance);
					const auto creditsNeeded = int64(form->invoice.credits)
						- int64(slice.balance);
					if (creditsNeeded <= 0) {
						sendBox();
					} else if (strong->session().premiumPossible()) {
						strong->uiShow()->show(Box(
							Settings::SmallBalanceBox,
							strong,
							creditsNeeded,
							form->botId,
							sendBox));
					} else {
						strong->uiShow()->showToast(
							tr::lng_credits_purchase_blocked(tr::now));
						if (maybeReturnToBot) {
							maybeReturnToBot(CheckoutResult::Failed);
						}
					}
				}
				lifetime->destroy();
			});
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
