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
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Payments {

bool IsCreditsInvoice(not_null<HistoryItem*> item) {
	if (const auto payment = item->Get<HistoryServicePayment>()) {
		return payment->isCreditsCurrency;
	}
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	return invoice && (invoice->currency == Ui::kCreditsCurrency);
}

void ProcessCreditsPayment(
		std::shared_ptr<Main::SessionShow> show,
		QPointer<QWidget> fireworks,
		std::shared_ptr<CreditsFormData> form,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto api = lifetime->make_state<Api::CreditsStatus>(
		show->session().user());
	const auto sendBox = [=] {
		const auto unsuccessful = std::make_shared<bool>(true);
		const auto box = show->show(Box(
			Ui::SendCreditsBox,
			form,
			[=] {
				*unsuccessful = false;
				if (const auto widget = fireworks.data()) {
					Ui::StartFireworks(widget);
				}
				if (maybeReturnToBot) {
					maybeReturnToBot(CheckoutResult::Paid);
				}
			}));
		box->boxClosing() | rpl::start_with_next([=] {
			crl::on_main([=] {
				if ((*unsuccessful) && maybeReturnToBot) {
					maybeReturnToBot(CheckoutResult::Cancelled);
				}
			});
		}, box->lifetime());
	};
	api->request({}, [=](Data::CreditsStatusSlice slice) {
		show->session().setCredits(slice.balance);
		const auto creditsNeeded = int64(form->invoice.credits)
			- int64(slice.balance);
		if (creditsNeeded <= 0) {
			sendBox();
		} else if (show->session().premiumPossible()) {
			show->show(Box(
				Settings::SmallBalanceBox,
				show,
				creditsNeeded,
				form->botId,
				sendBox));
		} else {
			show->showToast(
				tr::lng_credits_purchase_blocked(tr::now));
			if (maybeReturnToBot) {
				maybeReturnToBot(CheckoutResult::Failed);
			}
		}
		lifetime->destroy();
	});
}

void ProcessCreditsReceipt(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<CreditsReceiptData> receipt,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	const auto entry = Data::CreditsHistoryEntry{
		.id = receipt->id,
		.title = receipt->title,
		.description = receipt->description,
		.date = base::unixtime::parse(receipt->date),
		.photoId = receipt->photo ? receipt->photo->id : 0,
		.credits = receipt->credits,
		.bareMsgId = uint64(),
		.barePeerId = receipt->peerId.value,
		.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
	};
	controller->uiShow()->show(Box(
		Settings::ReceiptCreditsBox,
		controller,
		nullptr,
		entry));
	controller->window().activate();
}

Fn<void(NonPanelPaymentForm)> ProcessNonPanelPaymentFormFactory(
		not_null<Window::SessionController*> controller,
		Fn<void(CheckoutResult)> maybeReturnToBot) {
	return [=](NonPanelPaymentForm form) {
		using CreditsFormDataPtr = std::shared_ptr<CreditsFormData>;
		using CreditsReceiptPtr = std::shared_ptr<CreditsReceiptData>;
		v::match(form, [&](const CreditsFormDataPtr &form) {
			ProcessCreditsPayment(
				controller->uiShow(),
				controller->content().get(),
				form,
				maybeReturnToBot);
		}, [&](const CreditsReceiptPtr &receipt) {
			ProcessCreditsReceipt(controller, receipt, maybeReturnToBot);
		}, [](RealFormPresentedNotification) {});
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
